#include "syntax.h"

#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <regex.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <tree_sitter/api.h>

#include "buffer.h"
#include "display.h"
#include "hash.h"
#include "minibuffer.h"
#include "path.h"
#include "s8.h"
#include "settings.h"
#include "text.h"
#include "vec.h"

static char *treesitter_path[256] = {0};
static uint32_t treesitter_path_len = 0;
static const char *parser_filename = "parser";
static const char *highlight_path = "queries/highlights.scm";

struct predicate {
  uint32_t pattern_idx;

  bool (*eval)(struct s8, uint32_t, struct s8[], struct s8, void *);
  uint32_t argc;
  struct s8 argv[32];
  void *data;

  void (*cleanup)(void *);
};

struct highlight {
  TSParser *parser;
  TSTree *tree;
  TSQuery *query;
  VEC(struct predicate) predicates;
  void *dlhandle;
};

static void delete_parser(struct buffer *buffer, void *userdata) {
  struct highlight *highlight = (struct highlight *)userdata;

  if (highlight->query != NULL) {
    ts_query_delete(highlight->query);
  }

  VEC_FOR_EACH(&highlight->predicates, struct predicate * p) {
    if (p->cleanup != NULL) {
      p->cleanup(p->data);
    }
  }

  VEC_DESTROY(&highlight->predicates);

  ts_tree_delete(highlight->tree);
  ts_parser_delete(highlight->parser);

  dlclose(highlight->dlhandle);

  free(highlight);
}

static const char *read_text(void *payload, uint32_t byte_offset,
                             TSPoint position, uint32_t *bytes_read) {

  struct text *text = (struct text *)payload;

  if (position.row < text_num_lines(text)) {
    struct text_chunk chunk = text_get_line(text, position.row);

    // empty lines
    if (chunk.nbytes == 0 || position.column >= chunk.nbytes) {
      *bytes_read = 1;
      return "\n";
    }

    uint32_t bytei = position.column;
    *bytes_read = chunk.nbytes - bytei;
    return (const char *)chunk.text + bytei;
  }

  // eof
  *bytes_read = 0;
  return NULL;
}

static const char *grammar_name_from_buffer(struct buffer *buffer) {
  struct setting *s = lang_setting(&buffer->lang, "grammar");
  if (s != NULL && s->value.type == Setting_String) {
    return s->value.string_value;
  }

  return buffer->lang.name;
}

static const char *lang_folder(struct buffer *buffer, const char *path) {
  const char *langname = grammar_name_from_buffer(buffer);
  size_t tspath_len = strlen(path);
  size_t lang_len = strlen(langname);

  char *fld = malloc(tspath_len + lang_len + 2);
  uint32_t idx = 0;
  memcpy(&fld[idx], path, tspath_len);
  idx += tspath_len;
  fld[idx++] = '/';
  for (uint32_t i = 0; i < lang_len; ++i) {
    fld[idx + i] = tolower(langname[i]);
  }
  idx += lang_len;
  fld[idx++] = '\0';

  return fld;
}

static bool eval_eq(struct s8 capname, uint32_t argc, struct s8 argv[],
                    struct s8 value, void *data) {
  const struct s8 *cmp_to = (const struct s8 *)data;
  if (data == NULL) {
    return false;
  }

  return s8eq(value, *cmp_to);
}

static void cleanup_eq(void *data) {
  struct s8 *s = (struct s8 *)data;
  if (s != NULL) {
    free(s->s);
    s->l = 0;
    free(s);
  }
}

static bool eval_match(struct s8 capname, uint32_t argc, struct s8 argv[],
                       struct s8 value, void *data) {
  regex_t *regex = (regex_t *)data;
  if (regex == NULL) {
    return false;
  }

  char *text = s8tocstr(value);
  bool match = regexec(regex, text, 0, NULL, 0) == 0;

  free(text);
  return match;
}

static void cleanup_match(void *data) {
  regex_t *regex = (regex_t *)data;
  if (regex != NULL) {
    regfree(regex);
    free(regex);
  }
}

static void create_predicates(struct highlight *h, uint32_t pattern_index) {
  uint32_t npreds = 0;
  const TSQueryPredicateStep *predicate_steps =
      ts_query_predicates_for_pattern(h->query, pattern_index, &npreds);

  struct s8 capname;
  struct s8 args[32] = {0};
  uint32_t argc = 0;
  for (uint32_t predi = 0; predi < npreds; ++predi) {
    const TSQueryPredicateStep *step = &predicate_steps[predi];
    switch (step->type) {
    case TSQueryPredicateStepTypeCapture:
      capname.s = (char *)ts_query_capture_name_for_id(h->query, step->value_id,
                                                       &capname.l);
      break;

    case TSQueryPredicateStepTypeString:
      args[argc].s = (char *)ts_query_string_value_for_id(
          h->query, step->value_id, &args[argc].l);
      ++argc;
      break;

    case TSQueryPredicateStepTypeDone:
      if (s8eq(args[0], s8("match?"))) {
        regex_t *re = calloc(1, sizeof(regex_t));
        char *val = s8tocstr(args[1]);

        if (regcomp(re, val, 0) == 0) {
          VEC_APPEND(&h->predicates, struct predicate * pred);
          pred->pattern_idx = pattern_index;
          pred->eval = eval_match;
          pred->cleanup = cleanup_match;
          pred->argc = 1;
          pred->data = re;

          memset(pred->argv, 0, sizeof(struct s8) * 32);
          memcpy(pred->argv, args, sizeof(struct s8));
        } else {
          free(re);
        }

        free(val);
      } else if (s8eq(args[0], s8("eq?"))) {
        struct s8 *val = calloc(1, sizeof(struct s8));
        *val = s8dup(args[1]);
        VEC_APPEND(&h->predicates, struct predicate * pred);
        pred->pattern_idx = pattern_index;
        pred->eval = eval_eq;
        pred->cleanup = cleanup_eq;
        pred->argc = 1;
        pred->data = val;
      }

      argc = 0;
      break;
    }
  }
}

static TSQuery *setup_queries(const char *lang_root, TSTree *tree) {
  const char *filename = join_path(lang_root, highlight_path);

  // read queries from file
  int fd = open(filename, O_RDONLY);
  free((void *)filename);
  if (fd < 0) {
    return NULL;
  }

  size_t len = lseek(fd, 0, SEEK_END);
  void *data = mmap(0, len, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);

  if (data == NULL) {
    return NULL;
  }

  // run queries
  TSQueryError error = TSQueryErrorNone;
  uint32_t error_offset = 0;
  TSQuery *q = ts_query_new(ts_tree_language(tree), (char *)data, len,
                            &error_offset, &error);

  if (error != TSQueryErrorNone) {
    const char *msg = "unknown error";
    switch (error) {
    case TSQueryErrorSyntax:
      msg = "syntax error";
      break;
    case TSQueryErrorNodeType:
      msg = "node type";
      break;
    case TSQueryErrorField:
      msg = "error field";
      break;
    case TSQueryErrorCapture:
      msg = "capture";
      break;
    }

    // calculate line
    const char *chars = (const char *)data;
    uint64_t byteoff = 0;
    uint32_t lineno = 1;
    uint32_t colno = 0;
    while (byteoff < error_offset) {
      if (chars[byteoff] == '\n') {
        ++lineno;
        colno = 0;
      }
      ++colno;
      ++byteoff;
    }
    message("ts query error at (%d, %d): %s, %.*s", lineno, colno, msg);

    munmap(data, len);
    return NULL;
  }

  munmap(data, len);
  return q;
}

static bool eval_predicates(struct highlight *h, struct text *text,
                            TSPoint start, TSPoint end, uint32_t pattern_index,
                            struct s8 cname) {
  VEC_FOR_EACH(&h->predicates, struct predicate * p) {
    if (p->pattern_idx == pattern_index) {
      struct text_chunk txt =
          text_get_region(text, start.row, start.column, end.row, end.column);
      bool result =
          p->eval(cname, p->argc, p->argv,
                  (struct s8){.s = txt.text, .l = txt.nbytes}, p->data);

      if (txt.allocated) {
        free(txt.text);
      }

      if (!result) {
        return false;
      }
    }
  }

  return true;
}

#define match_cname(cname, capture)                                            \
  (s8eq(cname, s8(capture)) || s8startswith(cname, s8(capture ".")))

static void update_parser(struct buffer *buffer, void *userdata,
                          struct location origin, uint32_t width,
                          uint32_t height) {

  struct highlight *h = (struct highlight *)userdata;

  if (h->query == NULL) {
    return;
  }

  if (buffer_is_empty(buffer)) {
    return;
  }

  // take results and set text properties
  TSQueryCursor *cursor = ts_query_cursor_new();
  uint32_t end_line = origin.line + height >= buffer_num_lines(buffer)
                          ? buffer_num_lines(buffer) - 1
                          : origin.line + height;
  ts_query_cursor_set_point_range(
      cursor, (TSPoint){.row = origin.line, .column = origin.col},
      (TSPoint){.row = end_line, .column = buffer_num_chars(buffer, end_line)});
  ts_query_cursor_exec(cursor, h->query, ts_tree_root_node(h->tree));

  TSQueryMatch match;
  while (ts_query_cursor_next_match(cursor, &match)) {
    for (uint32_t capi = 0; capi < match.capture_count; ++capi) {
      const TSQueryCapture *cap = &match.captures[capi];
      TSPoint start = ts_node_start_point(cap->node);
      TSPoint end = ts_node_end_point(cap->node);

      struct s8 cname;
      cname.s =
          (char *)ts_query_capture_name_for_id(h->query, cap->index, &cname.l);

      if (!eval_predicates(h, buffer->text, start, end, match.pattern_index,
                           cname)) {
        continue;
      }

      bool highlight = false;
      uint32_t color = 0;
      if (match_cname(cname, "keyword")) {
        highlight = true;
        color = Color_Blue;
      } else if (match_cname(cname, "operator")) {
        highlight = true;
        color = Color_Magenta;
      } else if (match_cname(cname, "delimiter")) {
        highlight = false;
      } else if (s8eq(cname, s8("text"))) {
        highlight = false;
      } else if (match_cname(cname, "string") || match_cname(cname, "text")) {
        highlight = true;
        color = Color_Green;
      } else if (match_cname(cname, "constant")) {
        highlight = true;
        color = Color_Yellow;
      } else if (match_cname(cname, "attribute")) {
        highlight = true;
        color = Color_Yellow;
      } else if (match_cname(cname, "number")) {
        highlight = true;
        color = Color_Yellow;
      } else if (match_cname(cname, "function")) {
        highlight = true;
        color = Color_Yellow;
      } else if (match_cname(cname, "property")) {
        highlight = false;
      } else if (match_cname(cname, "label")) {
        highlight = false;
      } else if (match_cname(cname, "type")) {
        highlight = true;
        color = Color_Cyan;
      } else if (match_cname(cname, "variable")) {
        highlight = false;
      } else if (match_cname(cname, "comment")) {
        highlight = true;
        color = Color_BrightBlack;
      }

      if (!highlight) {
        continue;
      }

      buffer_add_text_property(
          buffer,
          (struct location){.line = start.row,
                            .col = text_byteindex_to_col(
                                buffer->text, start.row, start.column)},
          (struct location){.line = end.row,
                            .col = text_byteindex_to_col(buffer->text, end.row,
                                                         end.column - 1)},
          (struct text_property){
              .type = TextProperty_Colors,
              .colors =
                  (struct text_property_colors){
                      .set_fg = true,
                      .fg = color,
                  },
          });
    }
  }

  ts_query_cursor_delete(cursor);
}

static void text_removed(struct buffer *buffer, struct region removed,
                         uint32_t begin_idx, uint32_t end_idx, void *userdata) {
  struct highlight *h = (struct highlight *)userdata;

  TSPoint begin = {.row = removed.begin.line,
                   .column = text_col_to_byteindex(
                       buffer->text, removed.begin.line, removed.begin.col)};
  TSPoint new_end = begin;
  TSPoint old_end = {.row = removed.end.line,
                     .column = text_col_to_byteindex(
                         buffer->text, removed.end.line, removed.end.col)};

  TSInputEdit edit = {
      .start_point = begin,
      .old_end_point = old_end,
      .new_end_point = new_end,
      .start_byte = begin_idx,
      .old_end_byte = end_idx,
      .new_end_byte = begin_idx,
  };

  ts_tree_edit(h->tree, &edit);
  TSInput i = (TSInput){
      .payload = buffer->text,
      .read = read_text,
      .encoding = TSInputEncodingUTF8,
  };

  TSTree *new_tree = ts_parser_parse(h->parser, h->tree, i);
  if (new_tree != NULL) {
    ts_tree_delete(h->tree);
    h->tree = new_tree;
  }
}

static void buffer_reloaded(struct buffer *buffer, void *userdata) {
  struct highlight *h = (struct highlight *)userdata;

  TSInput i = (TSInput){
      .payload = buffer->text,
      .read = read_text,
      .encoding = TSInputEncodingUTF8,
  };

  TSTree *new_tree = ts_parser_parse(h->parser, NULL, i);
  if (new_tree != NULL) {
    ts_tree_delete(h->tree);
    h->tree = new_tree;
  }
}

static void text_inserted(struct buffer *buffer, struct region inserted,
                          uint32_t begin_idx, uint32_t end_idx,
                          void *userdata) {
  struct highlight *h = (struct highlight *)userdata;

  TSPoint begin = {.row = inserted.begin.line,
                   .column = text_col_to_byteindex(
                       buffer->text, inserted.begin.line, inserted.begin.col)};
  TSPoint old_end = begin;
  TSPoint new_end = {.row = inserted.end.line,
                     .column = text_col_to_byteindex(
                         buffer->text, inserted.end.line, inserted.end.col)};

  TSInputEdit edit = {
      .start_point = begin,
      .old_end_point = old_end,
      .new_end_point = new_end,
      .start_byte = begin_idx,
      .old_end_byte = begin_idx,
      .new_end_byte = end_idx,
  };

  ts_tree_edit(h->tree, &edit);
  TSInput i = (TSInput){
      .payload = buffer->text,
      .read = read_text,
      .encoding = TSInputEncodingUTF8,
  };

  TSTree *new_tree = ts_parser_parse(h->parser, h->tree, i);
  if (new_tree != NULL) {
    ts_tree_delete(h->tree);
    h->tree = new_tree;
  }
}

static void create_parser(struct buffer *buffer, void *userdata) {

  TSLanguage *(*langsym)() = NULL;
  const char *lang_root = NULL, *langname = NULL;
  void *h = NULL;

  for (uint32_t i = 0; i < treesitter_path_len && langsym == NULL; ++i) {
    const char *path = treesitter_path[i];
    lang_root = lang_folder(buffer, path);
    const char *filename = join_path(lang_root, parser_filename);

    h = dlopen(filename, RTLD_LAZY);
    free((void *)filename);
    if (h == NULL) {
      free((void *)lang_root);
      continue;
    }

    langname = grammar_name_from_buffer(buffer);
    size_t lang_len = strlen(langname);

    const char *prefix = "tree_sitter_";
    size_t prefix_len = strlen(prefix);
    char *function = malloc(prefix_len + lang_len + 1);
    memcpy(function, prefix, prefix_len);
    for (uint32_t i = 0; i < lang_len; ++i) {
      function[prefix_len + i] = tolower(langname[i]);
    }
    function[prefix_len + lang_len] = '\0';
    langsym = dlsym(h, function);

    free(function);
    if (langsym == NULL) {
      free((void *)lang_root);
      dlclose(h);
    }
  }

  if (langsym == NULL) {
    return;
  }

  struct highlight *hl =
      (struct highlight *)calloc(1, sizeof(struct highlight));
  hl->parser = ts_parser_new();
  ts_parser_set_language(hl->parser, langsym());

  TSInput i = (TSInput){
      .payload = buffer->text,
      .read = read_text,
      .encoding = TSInputEncodingUTF8,
  };
  hl->tree = ts_parser_parse(hl->parser, NULL, i);
  hl->query = setup_queries(lang_root, hl->tree);

  if (hl->query == NULL) {
    ts_parser_delete(hl->parser);
    free((void *)lang_root);
    return;
  }

  VEC_INIT(&hl->predicates, 8);
  uint32_t npatterns = ts_query_pattern_count(hl->query);
  for (uint32_t pi = 0; pi < npatterns; ++pi) {
    create_predicates(hl, pi);
  }
  hl->dlhandle = h;

  free((void *)lang_root);

  minibuffer_echo_timeout(4, "syntax set up for %s", langname);

  buffer_add_reload_hook(buffer, buffer_reloaded, hl);
  buffer_add_delete_hook(buffer, text_removed, hl);
  buffer_add_insert_hook(buffer, text_inserted, hl);
  buffer_add_render_hook(buffer, update_parser, hl);
  buffer_add_destroy_hook(buffer, delete_parser, hl);
}

void syntax_init(uint32_t grammar_path_len, const char *grammar_path[]) {

  treesitter_path_len = grammar_path_len < 256 ? grammar_path_len : 256;
  for (uint32_t i = 0; i < treesitter_path_len; ++i) {
    treesitter_path[i] = strdup(grammar_path[i]);
  }

  // special-case some of the built-in languages
  // that have grammar names different from the default
  struct language l = lang_from_id("gitcommit");
  if (!lang_is_fundamental(&l)) {
    lang_setting_set_default(
        &l, "grammar",
        (struct setting_value){.type = Setting_String,
                               .string_value = "gitcommit"});
    lang_destroy(&l);
  }

  l = lang_from_id("cxx");
  if (!lang_is_fundamental(&l)) {
    lang_setting_set_default(
        &l, "grammar",
        (struct setting_value){.type = Setting_String, .string_value = "cpp"});
    lang_destroy(&l);
  }

  buffer_add_create_hook(create_parser, NULL);
}

void syntax_teardown() {
  for (uint32_t i = 0; i < treesitter_path_len; ++i) {
    free((void *)treesitter_path[i]);
  }
}

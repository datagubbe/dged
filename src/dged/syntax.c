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
#include "hashmap.h"
#include "minibuffer.h"
#include "path.h"
#include "text.h"
#include "vec.h"

static char *treesitter_path = NULL;
static bool treesitter_path_allocated = false;
static const char *parser_filename = "parser";
static const char *highlight_path = "queries/highlights.scm";

HASHMAP_ENTRY_TYPE(re_cache_entry, regex_t);

struct highlight {
  TSParser *parser;
  TSTree *tree;
  TSQuery *query;
  HASHMAP(struct re_cache_entry) re_cache;
  void *dlhandle;
};

static void delete_parser(struct buffer *buffer, void *userdata) {
  struct highlight *highlight = (struct highlight *)userdata;

  if (highlight->query != NULL) {
    ts_query_delete(highlight->query);
  }

  HASHMAP_FOR_EACH(&highlight->re_cache, struct re_cache_entry * entry) {
    regfree(&entry->value);
  }

  HASHMAP_DESTROY(&highlight->re_cache);

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
    if (chunk.nbytes == 0 || position.column >= chunk.nchars) {
      *bytes_read = 1;
      return "\n";
    }

    uint32_t bytei = text_col_to_byteindex(text, chunk.line, position.column);
    *bytes_read = chunk.nbytes - bytei;
    return (const char *)chunk.text + bytei;
  }

  // eof
  *bytes_read = 0;
  return NULL;
}

static const char *lang_folder(struct buffer *buffer) {
  const char *langname = buffer->lang.name;

  size_t tspath_len = strlen(treesitter_path);
  size_t lang_len = strlen(langname);

  char *fld = malloc(tspath_len + lang_len + 2);
  uint32_t idx = 0;
  memcpy(&fld[idx], treesitter_path, tspath_len);
  idx += tspath_len;
  fld[idx++] = '/';
  for (uint32_t i = 0; i < lang_len; ++i) {
    fld[idx + i] = tolower(langname[i]);
  }
  idx += lang_len;
  fld[idx++] = '\0';

  return fld;
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

  munmap(data, len);

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
    message("ts query error at byte offset %d: %s", error_offset, msg);
    return NULL;
  }

  return q;
}

#define s8(s) ((struct s8){s, strlen(s)})

struct s8 {
  char *s;
  uint32_t l;
};

static bool s8eq(struct s8 s1, struct s8 s2) {
  return s1.l == s2.l && memcmp(s1.s, s2.s, s1.l) == 0;
}

char *s8tocstr(struct s8 s) {
  char *cstr = (char *)malloc(s.l + 1);
  memcpy(cstr, s.s, s.l);
  cstr[s.l] = '\0';
  return cstr;
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

struct predicate {
  bool (*fn)(struct s8, uint32_t, struct s8[], struct s8, void *);
  void (*cleanup)(void *);
  uint32_t argc;
  struct s8 argv[32];
  void *data;
};

typedef VEC(struct predicate) predicate_vec;

static regex_t *compile_re_cached(struct highlight *h, struct s8 expr) {
  char *val = s8tocstr(expr);
  HASHMAP_GET(&h->re_cache, struct re_cache_entry, val, regex_t * re);
  if (re == NULL) {
    regex_t new_re;
    if (regcomp(&new_re, val, 0) == 0) {
      HASHMAP_APPEND(&h->re_cache, struct re_cache_entry, val,
                     struct re_cache_entry * new);
      if (new != NULL) {
        new->value = new_re;
        re = &new->value;
      }
    }
  }

  free(val);
  return re;
}

static predicate_vec create_predicates(struct highlight *h,
                                       uint32_t pattern_index) {
  predicate_vec predicates;

  uint32_t npreds = 0;
  const TSQueryPredicateStep *predicate_steps =
      ts_query_predicates_for_pattern(h->query, pattern_index, &npreds);

  VEC_INIT(&predicates, 8);

  bool result = true;
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
        VEC_APPEND(&predicates, struct predicate * pred);
        pred->fn = eval_match;
        pred->cleanup = NULL;
        pred->argc = 1;

        // cache the regex
        pred->data = compile_re_cached(h, args[1]);

        memset(pred->argv, 0, sizeof(struct s8) * 32);
        memcpy(pred->argv, args, sizeof(struct s8));
      }

      argc = 0;
      break;
    }
  }

  return predicates;
}

static void update_parser(struct buffer *buffer, void *userdata,
                          struct location origin, uint32_t width,
                          uint32_t height) {

  struct highlight *h = (struct highlight *)userdata;

  if (h->query == NULL) {
    return;
  }

  // take results and set text properties
  // TODO: can reuse the cursor
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
    predicate_vec predicates = create_predicates(h, match.pattern_index);

    for (uint32_t capi = 0; capi < match.capture_count; ++capi) {
      const TSQueryCapture *cap = &match.captures[capi];
      TSPoint start = ts_node_start_point(cap->node);
      TSPoint end = ts_node_end_point(cap->node);

      struct s8 cname;
      cname.s =
          (char *)ts_query_capture_name_for_id(h->query, cap->index, &cname.l);

      bool predicates_match = true;
      VEC_FOR_EACH(&predicates, struct predicate * pred) {
        struct text_chunk txt = text_get_region(
            buffer->text, start.row, start.column, end.row, end.column);
        predicates_match &=
            pred->fn(cname, pred->argc, pred->argv,
                     (struct s8){.s = txt.text, .l = txt.nbytes}, pred->data);

        if (txt.allocated) {
          free(txt.text);
        }
      }

      if (!predicates_match) {
        continue;
      }

      bool highlight = false;
      uint32_t color = 0;
      if (s8eq(cname, s8("keyword"))) {
        highlight = true;
        color = Color_Blue;
      } else if (s8eq(cname, s8("operator"))) {
        highlight = true;
        color = Color_Magenta;
      } else if (s8eq(cname, s8("delimiter"))) {
        highlight = false;
      } else if (s8eq(cname, s8("string")) ||
                 s8eq(cname, s8("string.special")) ||
                 s8eq(cname, s8("string.special.path")) ||
                 s8eq(cname, s8("string.special.uri"))) {
        highlight = true;
        color = Color_Green;
      } else if (s8eq(cname, s8("constant"))) {
        highlight = true;
        color = Color_Yellow;
      } else if (s8eq(cname, s8("attribute"))) {
        highlight = true;
        color = Color_Yellow;
      } else if (s8eq(cname, s8("number"))) {
        highlight = false;
      } else if (s8eq(cname, s8("function")) ||
                 s8eq(cname, s8("function.macro")) ||
                 s8eq(cname, s8("function.method")) ||
                 s8eq(cname, s8("function.builtin")) ||
                 s8eq(cname, s8("function.special"))) {
        highlight = true;
        color = Color_Yellow;
      } else if (s8eq(cname, s8("property"))) {
        highlight = false;
      } else if (s8eq(cname, s8("label"))) {
        highlight = false;
      } else if (s8eq(cname, s8("type")) || s8eq(cname, s8("type.builtin"))) {
        highlight = true;
        color = Color_Cyan;
      } else if (s8eq(cname, s8("variable")) ||
                 s8eq(cname, s8("variable.builtin")) ||
                 s8eq(cname, s8("variable.parameter"))) {
        highlight = false;
      } else if (s8eq(cname, s8("comment"))) {
        highlight = true;
        color = Color_BrightBlack;
      }

      if (!highlight) {
        continue;
      }

      buffer_add_text_property(
          buffer, (struct location){.line = start.row, .col = start.column},
          (struct location){.line = end.row, .col = end.column - 1},
          (struct text_property){
              .type = TextProperty_Colors,
              .colors =
                  (struct text_property_colors){
                      .set_fg = true,
                      .fg = color,
                  },
          });
    }

    VEC_FOR_EACH(&predicates, struct predicate * pred) {
      if (pred->cleanup != NULL) {
        pred->cleanup(pred->data);
      }
    }
    VEC_DESTROY(&predicates);
  }

  ts_query_cursor_delete(cursor);
}

static void text_removed(struct buffer *buffer, struct region removed,
                         uint32_t begin_idx, uint32_t end_idx, void *userdata) {
  struct highlight *h = (struct highlight *)userdata;

  TSInputEdit edit = {
      .start_point =
          (TSPoint){.row = removed.begin.line, .column = removed.begin.col},
      .old_end_point =
          (TSPoint){.row = removed.end.line, .column = removed.end.col},
      .new_end_point =
          (TSPoint){.row = removed.begin.line, .column = removed.begin.col},
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

  TSInputEdit edit = {
      .start_point =
          (TSPoint){.row = inserted.begin.line, .column = inserted.begin.col},
      .old_end_point =
          (TSPoint){.row = inserted.begin.line, .column = inserted.begin.col},
      .new_end_point =
          (TSPoint){.row = inserted.end.line, .column = inserted.end.col},
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
  const char *lang_root = lang_folder(buffer);
  const char *filename = join_path(lang_root, parser_filename);

  void *h = dlopen(filename, RTLD_LAZY);
  free((void *)filename);
  if (h == NULL) {
    free((void *)lang_root);
    return;
  }

  const char *langname = buffer->lang.name;
  size_t lang_len = strlen(langname);

  const char *prefix = "tree_sitter_";
  size_t prefix_len = strlen(prefix);
  char *function = malloc(prefix_len + lang_len + 1);
  memcpy(function, prefix, prefix_len);
  for (uint32_t i = 0; i < lang_len; ++i) {
    function[prefix_len + i] = tolower(langname[i]);
  }
  function[prefix_len + lang_len] = '\0';
  TSLanguage *(*langsym)() = dlsym(h, function);

  free(function);
  if (langsym == NULL) {
    free((void *)lang_root);
    dlclose(h);
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
  hl->dlhandle = h;
  HASHMAP_INIT(&hl->re_cache, 64, hash_name);

  free((void *)lang_root);

  minibuffer_echo_timeout(4, "syntax set up for %s", langname);

  buffer_add_reload_hook(buffer, buffer_reloaded, hl);
  buffer_add_delete_hook(buffer, text_removed, hl);
  buffer_add_insert_hook(buffer, text_inserted, hl);
  buffer_add_render_hook(buffer, update_parser, hl);
  buffer_add_destroy_hook(buffer, delete_parser, hl);
}

#define xstr(s) str(s)
#define str(s) #s

void syntax_init() {
  treesitter_path = getenv("TREESITTER_GRAMMARS");
  if (treesitter_path == NULL) {
    treesitter_path = (char *)join_path(xstr(DATADIR), "grammars");
    treesitter_path_allocated = true;
  }

  struct stat buffer;
  if (stat(treesitter_path, &buffer) != 0) {
    minibuffer_echo_timeout(4,
                            "failed to initialize syntax, TREESITTER_GRAMMARS "
                            "not set and grammars dir does not exist at %s.",
                            treesitter_path);

    if (treesitter_path_allocated) {
      free(treesitter_path);
    }

    return;
  }

  buffer_add_create_hook(create_parser, NULL);
}

void syntax_teardown() {
  if (treesitter_path_allocated) {
    free(treesitter_path);
  }
}

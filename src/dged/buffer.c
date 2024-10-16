#include "buffer.h"
#include "binding.h"
#include "dged/vec.h"
#include "display.h"
#include "errno.h"
#include "lang.h"
#include "minibuffer.h"
#include "path.h"
#include "reactor.h"
#include "s8.h"
#include "settings.h"
#include "utf8.h"

#include <assert.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#define KILL_RING_SZ 64
static struct kill_ring {
  struct text_chunk buffer[KILL_RING_SZ];
  struct location last_paste;
  bool paste_up_to_date;
  uint32_t curr_idx;
  uint32_t paste_idx;
} g_kill_ring = {.curr_idx = 0,
                 .buffer = {{0}},
                 .last_paste = {0},
                 .paste_idx = 0,
                 .paste_up_to_date = false};

#define DECLARE_HOOK(name, callback_type, vec_type)                            \
  struct name##_hook {                                                         \
    uint32_t id;                                                               \
    callback_type callback;                                                    \
    void *userdata;                                                            \
  };                                                                           \
                                                                               \
  static uint32_t insert_##name##_hook(                                        \
      vec_type *hooks, uint32_t *id, callback_type callback, void *userdata) { \
    uint32_t iid = ++(*id);                                                    \
    struct name##_hook hook = (struct name##_hook){                            \
        .id = iid,                                                             \
        .callback = callback,                                                  \
        .userdata = userdata,                                                  \
    };                                                                         \
    VEC_PUSH(hooks, hook);                                                     \
                                                                               \
    return iid;                                                                \
  }                                                                            \
                                                                               \
  static void remove_##name##_hook(vec_type *hooks, uint32_t id,               \
                                   remove_hook_cb callback) {                  \
    uint64_t found_at = (uint64_t)-1;                                          \
    VEC_FOR_EACH_INDEXED(hooks, struct name##_hook *h, idx) {                  \
      if (h->id == id) {                                                       \
        if (callback != NULL) {                                                \
          callback(h->userdata);                                               \
        }                                                                      \
        found_at = idx;                                                        \
        break;                                                                 \
      }                                                                        \
    }                                                                          \
    if (found_at != (uint64_t)-1) {                                            \
      if (found_at < VEC_SIZE(hooks) - 1) {                                    \
        VEC_SWAP(hooks, found_at, VEC_SIZE(hooks) - 1);                        \
      }                                                                        \
      VEC_POP(hooks, struct name##_hook removed);                              \
      (void)removed;                                                           \
    }                                                                          \
  }

typedef VEC(struct create_hook) create_hook_vec;
typedef VEC(struct destroy_hook) destroy_hook_vec;
typedef VEC(struct insert_hook) insert_hook_vec;
typedef VEC(struct update_hook) update_hook_vec;
typedef VEC(struct reload_hook) reload_hook_vec;
typedef VEC(struct delete_hook) delete_hook_vec;
typedef VEC(struct render_hook) render_hook_vec;

DECLARE_HOOK(create, create_hook_cb, create_hook_vec)
DECLARE_HOOK(destroy, destroy_hook_cb, destroy_hook_vec)
DECLARE_HOOK(insert, insert_hook_cb, insert_hook_vec)
DECLARE_HOOK(update, update_hook_cb, update_hook_vec)
DECLARE_HOOK(reload, reload_hook_cb, reload_hook_vec)
DECLARE_HOOK(render, render_hook_cb, render_hook_vec)
DECLARE_HOOK(delete, delete_hook_cb, delete_hook_vec)

static create_hook_vec g_create_hooks;
uint32_t g_create_hook_id;

struct hooks {
  destroy_hook_vec destroy_hooks;
  uint32_t destroy_hook_id;

  insert_hook_vec insert_hooks;
  uint32_t insert_hook_id;

  update_hook_vec update_hooks;
  uint32_t update_hook_id;

  reload_hook_vec reload_hooks;
  uint32_t reload_hook_id;

  render_hook_vec render_hooks;
  uint32_t render_hook_id;

  delete_hook_vec delete_hooks;
  uint32_t delete_hook_id;
};

uint32_t buffer_add_create_hook(create_hook_cb callback, void *userdata) {
  return insert_create_hook(&g_create_hooks, &g_create_hook_id, callback,
                            userdata);
}

void buffer_remove_create_hook(uint32_t hook_id, remove_hook_cb callback) {
  remove_create_hook(&g_create_hooks, hook_id, callback);
}

uint32_t buffer_add_destroy_hook(struct buffer *buffer,
                                 destroy_hook_cb callback, void *userdata) {
  return insert_destroy_hook(&buffer->hooks->destroy_hooks,
                             &buffer->hooks->destroy_hook_id, callback,
                             userdata);
}

void buffer_remove_destroy_hook(struct buffer *buffer, uint32_t hook_id,
                                remove_hook_cb callback) {
  remove_destroy_hook(&buffer->hooks->destroy_hooks, hook_id, callback);
}

void buffer_static_init(void) {
  VEC_INIT(&g_create_hooks, 8);

  settings_set_default(
      "editor.tab-width",
      (struct setting_value){.type = Setting_Number, .data.number_value = 4});

  settings_set_default(
      "editor.show-whitespace",
      (struct setting_value){.type = Setting_Bool, .data.bool_value = true});
}

void buffer_static_teardown(void) {
  VEC_DESTROY(&g_create_hooks);
  for (uint32_t i = 0; i < KILL_RING_SZ; ++i) {
    if (g_kill_ring.buffer[i].allocated) {
      free(g_kill_ring.buffer[i].text);
    }
  }
}

static uint32_t get_tab_width(struct buffer *buffer) {
  struct setting *tw = lang_setting(&buffer->lang, "tab-width");
  if (tw == NULL) {
    tw = settings_get("editor.tab-width");
  }

  uint32_t tab_width = 4;
  if (tw != NULL && tw->value.type == Setting_Number) {
    tab_width = tw->value.data.number_value;
  }
  return tab_width;
}

static bool use_tabs(struct buffer *buffer) {
  struct setting *ut = lang_setting(&buffer->lang, "use-tabs");
  if (ut == NULL) {
    ut = settings_get("editor.use-tabs");
  }

  bool use_tabs = false;
  if (ut != NULL && ut->value.type == Setting_Bool) {
    use_tabs = ut->value.data.bool_value;
  }

  return use_tabs;
}

static uint32_t visual_char_width(struct codepoint *codepoint,
                                  uint32_t tab_width) {
  if (codepoint->codepoint == '\t') {
    return tab_width;
  } else {
    return unicode_visual_char_width(codepoint);
  }
}

static struct buffer create_internal(const char *name, char *filename) {
  struct buffer b = (struct buffer){
      .filename = filename,
      .name = strdup(name),
      .text = text_create(10),
      .modified = false,
      .readonly = false,
      .lazy_row_add = true,
      .lang =
          filename != NULL ? lang_from_filename(filename) : lang_from_id("fnd"),
      .last_write = {0},
  };

  b.hooks = calloc(1, sizeof(struct hooks));
  VEC_INIT(&b.hooks->insert_hooks, 8);
  VEC_INIT(&b.hooks->update_hooks, 8);
  VEC_INIT(&b.hooks->reload_hooks, 8);
  VEC_INIT(&b.hooks->render_hooks, 8);
  VEC_INIT(&b.hooks->delete_hooks, 8);
  VEC_INIT(&b.hooks->destroy_hooks, 8);

  undo_init(&b.undo, 100);

  return b;
}

static void strip_final_newline(struct buffer *b) {
  uint32_t nlines = text_num_lines(b->text);
  if (nlines > 0 && buffer_line_length(b, nlines - 1) == 0) {
    text_delete(b->text, nlines - 1, 0, nlines - 1, 1);
  }
}

static void buffer_read_from_file(struct buffer *b) {
  struct stat sb;
  char *fullname = to_abspath(b->filename);
  if (stat(fullname, &sb) == 0) {
    FILE *file = fopen(fullname, "r");
    free(fullname);

    if (file == NULL) {
      minibuffer_echo("Error opening %s: %s", b->filename, strerror(errno));
      return;
    }

    while (true) {
      uint8_t buff[4096];
      int bytes = fread(buff, 1, 4096, file);
      if (bytes > 0) {
        uint32_t ignore;
        text_append(b->text, buff, bytes, &ignore);
      } else if (bytes == 0) {
        break; // EOF
      } else {
        minibuffer_echo("error reading from %s: %s", b->filename,
                        strerror(errno));
        fclose(file);
        return;
      }
    }

    fclose(file);

    // if last line is empty, remove it
    strip_final_newline(b);
  } else {
    minibuffer_echo("Error opening %s: %s", b->filename, strerror(errno));
    free(fullname);
    return;
  }

  undo_push_boundary(&b->undo, (struct undo_boundary){.save_point = true});
}

static void write_line(struct text_chunk *chunk, void *userdata) {
  FILE *file = (FILE *)userdata;
  fwrite(chunk->text, 1, chunk->nbytes, file);

  // final newline is not optional!
  fputc('\n', file);
}

static bool is_word_break(const struct codepoint *codepoint) {
  uint32_t c = codepoint->codepoint;
  return c == ' ' || c == '.' || c == '(' || c == ')' || c == '[' || c == ']' ||
         c == '{' || c == '}' || c == ';' || c == '<' || c == '>' || c == ':' ||
         c == '"';
}

static bool is_word_char(const struct codepoint *c) {
  return !is_word_break(c);
}

static struct match_result
find_next_in_line(struct buffer *buffer, struct location start,
                  bool (*predicate)(const struct codepoint *c)) {
  if (text_line_size(buffer->text, start.line) == 0) {
    return (struct match_result){.at = start, .found = false};
  }

  bool found = false;
  struct utf8_codepoint_iterator iter =
      text_line_codepoint_iterator(buffer->text, start.line);
  uint32_t coli = 0, tab_width = get_tab_width(buffer);
  struct codepoint *codepoint;
  while ((codepoint = utf8_next_codepoint(&iter)) != NULL) {
    if (coli >= start.col && predicate(codepoint)) {
      found = true;
      break;
    }

    coli += visual_char_width(codepoint, tab_width);
  }

  return (struct match_result){
      .at = (struct location){.line = start.line, .col = coli}, .found = found};
}

static struct match_result
find_prev_in_line(struct buffer *buffer, struct location start,
                  bool (*predicate)(const struct codepoint *c)) {

  if (text_line_size(buffer->text, start.line) == 0) {
    return (struct match_result){.at = start, .found = false};
  }

  bool found = false;
  struct utf8_codepoint_iterator iter =
      text_line_codepoint_iterator(buffer->text, start.line);
  uint32_t coli = 0, tab_width = get_tab_width(buffer), found_at;
  struct codepoint *codepoint;
  while (coli < start.col && (codepoint = utf8_next_codepoint(&iter)) != NULL) {
    if (predicate(codepoint)) {
      found = true;
      found_at = coli;
    }
    coli += visual_char_width(codepoint, tab_width);
  }

  return (struct match_result){
      .at =
          (struct location){.line = start.line, .col = found ? found_at : coli},
      .found = found};
}

static struct text_chunk *copy_region(struct buffer *buffer,
                                      struct region region) {
  struct text_chunk *curr = &g_kill_ring.buffer[g_kill_ring.curr_idx];
  g_kill_ring.curr_idx = (g_kill_ring.curr_idx + 1) % KILL_RING_SZ;

  if (curr->allocated) {
    free(curr->text);
  }

  struct location begin_bytes =
      buffer_location_to_byte_coords(buffer, region.begin);
  struct location end_bytes =
      buffer_location_to_byte_coords(buffer, region.end);

  struct text_chunk txt =
      text_get_region(buffer->text, begin_bytes.line, begin_bytes.col,
                      end_bytes.line, end_bytes.col);
  *curr = txt;
  return curr;
}

static struct location do_indent(struct buffer *buffer, struct location at,
                                 uint32_t tab_width, bool use_tabs) {
  if (use_tabs) {
    return buffer_add(buffer, at, (uint8_t *)"\t", 1);
  } else {
    return buffer_add(buffer, at, (uint8_t *)"                ",
                      tab_width > 16 ? 16 : tab_width);
  }
}

static uint64_t to_global_offset(struct buffer *buffer,
                                 struct location bytecoords) {
  uint32_t line = bytecoords.line;
  uint32_t col = bytecoords.col;
  uint32_t byteoff = 0;
  uint32_t nlines = buffer_num_lines(buffer);

  if (nlines == 0) {
    return 0;
  }

  for (uint32_t l = 0; l < line && l < nlines; ++l) {
    // +1 for newline
    byteoff += text_line_size(buffer->text, l) + 1;
  }

  // handle last line
  uint32_t l = line < nlines ? line : nlines - 1;
  uint32_t nbytes = text_line_size(buffer->text, l);
  byteoff += col <= nbytes ? col : nbytes + 1;

  return byteoff;
}

/* --------------------- buffer methods -------------------- */

struct buffer buffer_create(const char *name) {

  struct buffer b = create_internal(name, NULL);

  VEC_FOR_EACH(&g_create_hooks, struct create_hook * h) {
    h->callback(&b, h->userdata);
  }

  return b;
}

struct buffer buffer_from_file(const char *path) {
  char *full_path = to_abspath(path);
  struct buffer b = create_internal(basename((char *)path), full_path);
  buffer_read_from_file(&b);

  VEC_FOR_EACH(&g_create_hooks, struct create_hook * h) {
    h->callback(&b, h->userdata);
  }

  return b;
}

void buffer_to_file(struct buffer *buffer) {
  if (!buffer->filename) {
    minibuffer_echo("buffer \"%s\" is not associated with a file",
                    buffer->name);
    return;
  }

  if (!buffer->modified) {
    minibuffer_echo_timeout(4, "buffer already saved");
    return;
  }

  char *fullname = expanduser(buffer->filename);
  FILE *file = fopen(fullname, "w");
  free(fullname);
  if (file == NULL) {
    minibuffer_echo("failed to open file %s for writing: %s", buffer->filename,
                    strerror(errno));
    return;
  }

  uint32_t nlines = text_num_lines(buffer->text);
  uint32_t nlines_to_write = nlines;
  if (nlines > 0) {
    struct text_chunk lastline = text_get_line(buffer->text, nlines - 1);
    nlines_to_write = lastline.nbytes == 0 ? nlines - 1 : nlines;
    text_for_each_line(buffer->text, 0, nlines_to_write, write_line, file);
  }

  minibuffer_echo_timeout(4, "wrote %d lines to %s", nlines_to_write,
                          buffer->filename);
  fclose(file);

  clock_gettime(CLOCK_REALTIME, &buffer->last_write);
  buffer->modified = false;
  undo_push_boundary(&buffer->undo, (struct undo_boundary){.save_point = true});
}

void buffer_set_filename(struct buffer *buffer, const char *filename) {
  buffer->filename = to_abspath(filename);
  buffer->modified = true;
}

void buffer_reload(struct buffer *buffer) {
  if (buffer->filename == NULL) {
    return;
  }

  // check if we actually need to reload
  struct stat sb;
  if (stat(buffer->filename, &sb) < 0) {
    minibuffer_echo_timeout(4, "failed to run stat on %s", buffer->filename);
    return;
  }

  if (sb.st_mtim.tv_sec != buffer->last_write.tv_sec ||
      sb.st_mtim.tv_nsec != buffer->last_write.tv_nsec) {
    text_clear(buffer->text);
    buffer_read_from_file(buffer);
    VEC_FOR_EACH(&buffer->hooks->reload_hooks, struct reload_hook * h) {
      h->callback(buffer, h->userdata);
    }
  }
}

void buffer_destroy(struct buffer *buffer) {
  VEC_FOR_EACH(&buffer->hooks->destroy_hooks, struct destroy_hook * h) {
    h->callback(buffer, h->userdata);
  }

  lang_destroy(&buffer->lang);

  text_destroy(buffer->text);
  buffer->text = NULL;

  free(buffer->name);
  buffer->name = NULL;

  free(buffer->filename);
  buffer->filename = NULL;

  VEC_DESTROY(&buffer->hooks->update_hooks);
  VEC_DESTROY(&buffer->hooks->render_hooks);
  VEC_DESTROY(&buffer->hooks->reload_hooks);
  VEC_DESTROY(&buffer->hooks->insert_hooks);
  VEC_DESTROY(&buffer->hooks->destroy_hooks);
  VEC_DESTROY(&buffer->hooks->delete_hooks);
  free(buffer->hooks);

  undo_destroy(&buffer->undo);
}

struct location buffer_add(struct buffer *buffer, struct location at,
                           uint8_t *text, uint32_t nbytes) {
  if (buffer->readonly) {
    minibuffer_echo_timeout(4, "buffer is read-only");
    return at;
  }

  // invalidate last paste
  g_kill_ring.paste_up_to_date = false;

  struct location initial = at;
  struct location final = at;

  struct location at_bytes = buffer_location_to_byte_coords(buffer, at);

  uint32_t lines_added;
  text_insert_at(buffer->text, at_bytes.line, at_bytes.col, text, nbytes,
                 &lines_added);

  // move to after inserted text
  if (lines_added > 0) {
    final = buffer_clamp(buffer, (int64_t)at.line + lines_added, 0);
  } else {
    uint32_t cols_added = 0, tab_width = get_tab_width(buffer);
    struct utf8_codepoint_iterator iter =
        create_utf8_codepoint_iterator(text, nbytes, 0);
    struct codepoint *codepoint;
    while ((codepoint = utf8_next_codepoint(&iter)) != NULL) {
      cols_added += visual_char_width(codepoint, tab_width);
    }
    final =
        buffer_clamp(buffer, (int64_t)at.line, (int64_t)at.col + cols_added);
  }

  struct location final_bytes = buffer_location_to_byte_coords(buffer, final);

  undo_push_add(
      &buffer->undo,
      (struct undo_add){.begin = {.row = initial.line, .col = initial.col},
                        .end = {.row = final.line, .col = final.col}});

  if (lines_added > 0) {
    undo_push_boundary(&buffer->undo,
                       (struct undo_boundary){.save_point = false});
  }

  uint32_t begin_idx = to_global_offset(buffer, at_bytes);
  uint32_t end_idx = to_global_offset(buffer, final_bytes);

  VEC_FOR_EACH(&buffer->hooks->insert_hooks, struct insert_hook * h) {
    h->callback(buffer,
                (struct edit_location){
                    .coordinates = region_new(initial, final),
                    .bytes = region_new(at_bytes, final_bytes),
                    .global_byte_begin = begin_idx,
                    .global_byte_end = end_idx,
                },
                h->userdata);
  }

  buffer->modified = true;
  return final;
}

struct location buffer_set_text(struct buffer *buffer, uint8_t *text,
                                uint32_t nbytes) {
  uint32_t lines_added;

  text_clear(buffer->text);
  text_append(buffer->text, text, nbytes, &lines_added);

  // if last line is empty, remove it
  strip_final_newline(buffer);

  return buffer_clamp(buffer, lines_added,
                      buffer_line_length(buffer, lines_added));
}

void buffer_clear(struct buffer *buffer) { text_clear(buffer->text); }

bool buffer_is_empty(struct buffer *buffer) {
  return text_num_lines(buffer->text) == 0;
}

bool buffer_is_modified(struct buffer *buffer) { return buffer->modified; }
bool buffer_is_readonly(struct buffer *buffer) { return buffer->readonly; }

void buffer_set_readonly(struct buffer *buffer, bool readonly) {
  buffer->readonly = readonly;
}

bool buffer_is_backed(struct buffer *buffer) {
  return buffer->filename != NULL;
}

struct location buffer_previous_char(struct buffer *buffer,
                                     struct location dot) {
  if (dot.col == 0) {
    if (dot.line == 0) {
      return dot;
    }

    --dot.line;
    dot.col = buffer_line_length(buffer, dot.line);
  } else {
    struct utf8_codepoint_iterator iter =
        text_line_codepoint_iterator(buffer->text, dot.line);
    struct codepoint *codepoint;
    uint32_t coli = 0, tab_width = get_tab_width(buffer), last_width = 0;
    while (coli < dot.col && (codepoint = utf8_next_codepoint(&iter)) != NULL) {
      last_width = visual_char_width(codepoint, tab_width);
      coli += last_width;
    }

    dot.col = coli - last_width;
  }

  return dot;
}

struct location buffer_previous_word(struct buffer *buffer,
                                     struct location dot) {

  struct match_result res = find_prev_in_line(buffer, dot, is_word_break);
  if (!res.found && res.at.col == dot.col) {
    return buffer_previous_char(buffer, res.at);
  }

  // check if we got here from the middle of a word or not
  uint32_t traveled = dot.col - res.at.col;

  // if not, skip over another word
  if (traveled <= 1) {
    res = find_prev_in_line(buffer, res.at, is_word_char);
    if (!res.found) {
      return buffer_previous_char(buffer, res.at);
    }

    // at this point, we are at the end of the previous word
    res = find_prev_in_line(buffer, res.at, is_word_break);
    if (!res.found) {
      return res.at;
    } else {
      res.at = buffer_next_char(buffer, res.at);
    }
  } else {
    res.at = buffer_next_char(buffer, res.at);
  }

  return res.at;
}

struct location buffer_previous_line(struct buffer *buffer,
                                     struct location dot) {
  (void)buffer;

  if (dot.line <= 0) {
    dot.line = 0;
    return dot;
  }

  --dot.line;
  return dot;
}

struct location buffer_next_char(struct buffer *buffer, struct location dot) {
  if (dot.col == buffer_line_length(buffer, dot.line)) {
    uint32_t lastline = buffer->lazy_row_add ? buffer_num_lines(buffer)
                                             : buffer_num_lines(buffer) - 1;
    if (dot.line == lastline) {
      return dot;
    }

    dot.col = 0;
    ++dot.line;
  } else {
    struct utf8_codepoint_iterator iter =
        text_line_codepoint_iterator(buffer->text, dot.line);
    struct codepoint *codepoint;
    uint32_t coli = 0;
    while (coli <= dot.col &&
           (codepoint = utf8_next_codepoint(&iter)) != NULL) {
      coli += visual_char_width(codepoint, get_tab_width(buffer));
    }

    dot.col = coli;
  }

  return dot;
}

struct region buffer_word_at(struct buffer *buffer, struct location at) {
  struct match_result prev_word_break =
      find_prev_in_line(buffer, at, is_word_break);
  struct match_result next_word_break =
      find_next_in_line(buffer, at, is_word_break);

  if (prev_word_break.at.col != next_word_break.at.col &&
      prev_word_break.found) {
    prev_word_break.at = buffer_next_char(buffer, prev_word_break.at);
  }

  return region_new(prev_word_break.at, next_word_break.at);
}

struct location buffer_next_word(struct buffer *buffer, struct location dot) {
  struct match_result res = find_next_in_line(buffer, dot, is_word_break);
  if (!res.found) {
    return buffer_next_char(buffer, res.at);
  }

  uint32_t traveled = dot.col - res.at.col;

  res = find_next_in_line(buffer, res.at, is_word_char);

  // make a stop at the end of the line as well
  if (!res.found && traveled == 0) {
    res.at = buffer_next_char(buffer, res.at);
  }

  return res.at;
}

struct location buffer_next_line(struct buffer *buffer, struct location dot) {
  uint32_t lastline = buffer->lazy_row_add ? buffer_num_lines(buffer)
                                           : buffer_num_lines(buffer) - 1;
  if (dot.line == lastline) {
    return dot;
  }

  ++dot.line;
  uint32_t new_col = dot.col;
  uint32_t nchars = buffer_line_length(buffer, dot.line);
  new_col = new_col > nchars ? nchars : new_col;

  return dot;
}

struct location buffer_clamp(struct buffer *buffer, int64_t line, int64_t col) {
  struct location location = {.line = 0, .col = 0};
  if (buffer_is_empty(buffer)) {
    return location;
  }

  // clamp line
  if (line >= buffer_num_lines(buffer)) {
    if (buffer->lazy_row_add) {
      line = buffer_num_lines(buffer);

      // the "new" line is always empty
      col = 0;
    } else {
      line = buffer_num_lines(buffer) - 1;
    }
  } else if (line < 0) {
    line = 0;
  }

  // clamp col
  if (col < 0) {
    col = 0;
  } else if (col > buffer_line_length(buffer, line)) {
    col = buffer_line_length(buffer, line);
  }

  location.col = col;
  location.line = line;

  return location;
}

struct location buffer_end(struct buffer *buffer) {
  uint32_t nlines = buffer_num_lines(buffer);

  if (buffer->lazy_row_add) {
    return (struct location){.line = nlines, .col = 0};
  } else {
    nlines = nlines == 0 ? 0 : nlines - 1;
    return (struct location){.line = nlines,
                             .col = buffer_line_length(buffer, nlines)};
  }
}

uint32_t buffer_num_lines(struct buffer *buffer) {
  return text_num_lines(buffer->text);
}

uint32_t buffer_line_length(struct buffer *buffer, uint32_t line) {
  uint32_t tab_size = get_tab_width(buffer), len = 0;
  struct utf8_codepoint_iterator iter =
      text_line_codepoint_iterator(buffer->text, line);
  struct codepoint *codepoint;
  while ((codepoint = utf8_next_codepoint(&iter)) != NULL) {
    len += visual_char_width(codepoint, tab_size);
  }

  return len;
}

struct location buffer_newline(struct buffer *buffer, struct location at) {
  return buffer_add(buffer, at, (uint8_t *)"\n", 1);
}

struct location buffer_indent(struct buffer *buffer, struct location at) {
  return do_indent(buffer, at, get_tab_width(buffer), use_tabs(buffer));
}

struct location buffer_indent_alt(struct buffer *buffer, struct location at) {
  return do_indent(buffer, at, get_tab_width(buffer), !use_tabs(buffer));
}

struct location buffer_undo(struct buffer *buffer, struct location dot) {
  struct undo_stack *undo = &buffer->undo;
  undo_begin(undo);

  // fetch and handle records
  struct undo_record *records = NULL;
  uint32_t nrecords = 0;

  if (undo_current_position(undo) == INVALID_TOP) {
    minibuffer_echo_timeout(4,
                            "no more undo information, starting from top...");
  }

  undo_next(undo, &records, &nrecords);

  struct location pos = dot;
  undo_push_boundary(undo, (struct undo_boundary){.save_point = false});
  for (uint32_t reci = 0; reci < nrecords; ++reci) {
    struct undo_record *rec = &records[reci];
    switch (rec->type) {

    case Undo_Boundary: {
      struct undo_boundary *b = &rec->data.boundary;
      if (b->save_point) {
        buffer->modified = false;
      }
      break;
    }

    case Undo_Add: {
      struct undo_add *add = &rec->data.add;

      pos = buffer_delete(buffer,
                          (struct region){
                              .begin = (struct location){.line = add->begin.row,
                                                         .col = add->begin.col},
                              .end = (struct location){.line = add->end.row,
                                                       .col = add->end.col},
                          });

      break;
    }

    case Undo_Delete: {
      struct undo_delete *del = &rec->data.delete;
      pos = buffer_add(buffer,
                       (struct location){
                           .line = del->pos.row,
                           .col = del->pos.col,
                       },
                       del->data, del->nbytes);
      break;
    }
    }
  }

  undo_push_boundary(undo, (struct undo_boundary){.save_point = false});

  free(records);
  undo_end(undo);

  return pos;
}

/* --------------- searching and supporting types ---------------- */
struct search_data {
  VEC(struct region) matches;
  const char *pattern;
};

// TODO: maybe should live in text
static void search_line(struct text_chunk *chunk, void *userdata) {
  struct search_data *data = (struct search_data *)userdata;
  size_t pattern_len = strlen(data->pattern);
  uint32_t pattern_nchars = utf8_nchars((uint8_t *)data->pattern, pattern_len);

  char *line = malloc(chunk->nbytes + 1);
  strncpy(line, (const char *)chunk->text, chunk->nbytes);
  line[chunk->nbytes] = '\0';
  char *hit = NULL;
  uint32_t byteidx = 0;
  while ((hit = strstr(line + byteidx, data->pattern)) != NULL) {
    byteidx = hit - line;
    uint32_t begin = utf8_nchars(chunk->text, byteidx);
    struct region match =
        region_new((struct location){.col = begin, .line = chunk->line},
                   (struct location){.col = begin + pattern_nchars - 1,
                                     .line = chunk->line});
    VEC_PUSH(&data->matches, match);

    // proceed to after match
    byteidx += pattern_len;
  }

  free(line);
}

void buffer_find(struct buffer *buffer, const char *pattern,
                 struct region **matches, uint32_t *nmatches) {

  struct search_data data = (struct search_data){.pattern = pattern};
  VEC_INIT(&data.matches, 16);
  text_for_each_line(buffer->text, 0, text_num_lines(buffer->text), search_line,
                     &data);

  *matches = VEC_ENTRIES(&data.matches);
  *nmatches = VEC_SIZE(&data.matches);

  VEC_DISOWN_ENTRIES(&data.matches);
  VEC_DESTROY(&data.matches);
}

struct location buffer_copy(struct buffer *buffer, struct region region) {
  if (region_has_size(region)) {
    copy_region(buffer, region);
  }

  return region.begin;
}

struct location buffer_cut(struct buffer *buffer, struct region region) {
  if (region_has_size(region)) {
    copy_region(buffer, region);
    buffer_delete(buffer, region);
  }

  return region.begin;
}

struct location buffer_delete(struct buffer *buffer, struct region region) {
  if (buffer->readonly) {
    minibuffer_echo_timeout(4, "buffer is read-only");
    return region.begin;
  }

  if (!region_has_size(region)) {
    return region.begin;
  }

  struct location begin_bytes =
      buffer_location_to_byte_coords(buffer, region.begin);
  struct location end_bytes =
      buffer_location_to_byte_coords(buffer, region.end);

  struct text_chunk txt =
      text_get_region(buffer->text, begin_bytes.line, begin_bytes.col,
                      end_bytes.line, end_bytes.col);

  undo_push_boundary(&buffer->undo,
                     (struct undo_boundary){.save_point = false});

  undo_push_delete(&buffer->undo,
                   (struct undo_delete){.data = txt.text,
                                        .nbytes = txt.nbytes,
                                        .pos = {.row = region.begin.line,
                                                .col = region.begin.col}});
  undo_push_boundary(&buffer->undo,
                     (struct undo_boundary){.save_point = false});

  uint64_t begin_idx = to_global_offset(buffer, begin_bytes);
  uint64_t end_idx = to_global_offset(buffer, end_bytes);

  text_delete(buffer->text, begin_bytes.line, begin_bytes.col, end_bytes.line,
              end_bytes.col);
  buffer->modified = true;

  VEC_FOR_EACH(&buffer->hooks->delete_hooks, struct delete_hook * h) {
    h->callback(buffer,
                (struct edit_location){
                    .coordinates = region,
                    .bytes = region_new(begin_bytes, end_bytes),
                    .global_byte_begin = begin_idx,
                    .global_byte_end = end_idx,
                },
                h->userdata);
  }

  return region.begin;
}

static struct location paste(struct buffer *buffer, struct location at,
                             uint32_t ring_idx) {
  struct location new_loc = at;
  if (ring_idx > 0) {
    struct text_chunk *curr = &g_kill_ring.buffer[ring_idx - 1];
    if (curr->text != NULL) {
      g_kill_ring.last_paste = at;
      new_loc = buffer_add(buffer, at, curr->text, curr->nbytes);
      g_kill_ring.paste_up_to_date = true;
    }
  }

  return new_loc;
}

struct location buffer_paste(struct buffer *buffer, struct location at) {
  g_kill_ring.paste_idx = g_kill_ring.curr_idx;
  return paste(buffer, at, g_kill_ring.curr_idx);
}

struct location buffer_paste_older(struct buffer *buffer, struct location at) {
  if (g_kill_ring.paste_up_to_date) {

    // remove previous paste
    buffer_delete(buffer, region_new(g_kill_ring.last_paste, at));

    // paste older
    if (g_kill_ring.paste_idx - 1 > 0) {
      --g_kill_ring.paste_idx;
    } else {
      g_kill_ring.paste_idx = g_kill_ring.curr_idx;
    }

    return paste(buffer, g_kill_ring.last_paste, g_kill_ring.paste_idx);

  } else {
    return buffer_paste(buffer, at);
  }
}

struct text_chunk buffer_line(struct buffer *buffer, uint32_t line) {
  return text_get_line(buffer->text, line);
}

struct text_chunk buffer_region(struct buffer *buffer, struct region region) {
  return text_get_region(buffer->text, region.begin.line, region.begin.col,
                         region.end.line, region.end.col);
}

uint32_t buffer_add_insert_hook(struct buffer *buffer, insert_hook_cb hook,
                                void *userdata) {
  return insert_insert_hook(&buffer->hooks->insert_hooks,
                            &buffer->hooks->insert_hook_id, hook, userdata);
}

void buffer_remove_insert_hook(struct buffer *buffer, uint32_t hook_id,
                               remove_hook_cb callback) {
  remove_insert_hook(&buffer->hooks->insert_hooks, hook_id, callback);
}

uint32_t buffer_add_delete_hook(struct buffer *buffer, delete_hook_cb hook,
                                void *userdata) {
  return insert_delete_hook(&buffer->hooks->delete_hooks,
                            &buffer->hooks->delete_hook_id, hook, userdata);
}

void buffer_remove_delete_hook(struct buffer *buffer, uint32_t hook_id,
                               remove_hook_cb callback) {
  remove_delete_hook(&buffer->hooks->delete_hooks, hook_id, callback);
}

uint32_t buffer_add_update_hook(struct buffer *buffer, update_hook_cb hook,
                                void *userdata) {
  return insert_update_hook(&buffer->hooks->update_hooks,
                            &buffer->hooks->update_hook_id, hook, userdata);
}

void buffer_remove_update_hook(struct buffer *buffer, uint32_t hook_id,
                               remove_hook_cb callback) {
  remove_update_hook(&buffer->hooks->update_hooks, hook_id, callback);
}

uint32_t buffer_add_render_hook(struct buffer *buffer, render_hook_cb callback,
                                void *userdata) {
  return insert_render_hook(&buffer->hooks->render_hooks,
                            &buffer->hooks->render_hook_id, callback, userdata);
}

void buffer_remove_render_hook(struct buffer *buffer, uint32_t hook_id,
                               remove_hook_cb callback) {
  remove_render_hook(&buffer->hooks->render_hooks, hook_id, callback);
}

uint32_t buffer_add_reload_hook(struct buffer *buffer, reload_hook_cb callback,
                                void *userdata) {
  return insert_reload_hook(&buffer->hooks->reload_hooks,
                            &buffer->hooks->reload_hook_id, callback, userdata);
}

void buffer_remove_reload_hook(struct buffer *buffer, uint32_t hook_id,
                               remove_hook_cb callback) {
  remove_reload_hook(&buffer->hooks->reload_hooks, hook_id, callback);
}

struct cmdbuf {
  struct command_list *cmds;
  struct location origin;
  uint32_t width;
  uint32_t height;

  bool show_ws;

  struct buffer *buffer;
};

static void apply_properties(struct command_list *cmds,
                             struct text_property *properties[],
                             uint32_t nproperties) {
  for (uint32_t propi = 0; propi < nproperties; ++propi) {
    struct text_property *prop = properties[propi];

    switch (prop->type) {
    case TextProperty_Colors: {
      struct text_property_colors *colors = &prop->data.colors;
      if (colors->set_bg) {
        command_list_set_index_color_bg(cmds, colors->bg);
      }

      if (colors->set_fg) {
        command_list_set_index_color_fg(cmds, colors->fg);
      }
      break;
    }
    case TextProperty_Data:
      break;
    }
  }
}

static uint64_t properties_hash(struct text_property *properties[],
                                uint32_t nproperties) {
  uint64_t hash = 0;
  for (uint32_t i = 0; i < nproperties; ++i) {
    hash += (uint64_t)properties[i];
  }

  return hash;
}

void render_line(struct text_chunk *line, void *userdata) {
  struct cmdbuf *cmdbuf = (struct cmdbuf *)userdata;
  uint32_t visual_line = line->line - cmdbuf->origin.line;

  command_list_set_show_whitespace(cmdbuf->cmds, cmdbuf->show_ws);

  // calculate scroll offsets
  struct text_property *properties[32] = {0};
  uint64_t prev_properties_hash = 0;

  uint32_t tab_width = get_tab_width(cmdbuf->buffer);

  // handle scroll column offset
  uint32_t coli = 0, bytei = 0;
  struct utf8_codepoint_iterator iter = text_chunk_codepoint_iterator(line);
  struct codepoint *codepoint;
  while (coli < cmdbuf->origin.col &&
         (codepoint = utf8_next_codepoint(&iter)) != NULL) {
    coli += visual_char_width(codepoint, tab_width);
    bytei += codepoint->nbytes;
  }

  // coli is the visual column [0..width-1]
  coli = 0;
  uint32_t drawn_bytei = bytei;
  uint32_t drawn_coli = coli;

  while (coli < cmdbuf->width &&
         (codepoint = utf8_next_codepoint(&iter)) != NULL) {
    // calculate character properties
    uint32_t nproperties = 0;
    text_get_properties(cmdbuf->buffer->text, line->line, bytei, properties, 32,
                        &nproperties);

    // if we have any new or lost props, flush text up until now, reset
    // and re-apply current properties
    uint64_t new_properties_hash = properties_hash(properties, nproperties);
    if (new_properties_hash != prev_properties_hash) {
      command_list_draw_text(cmdbuf->cmds, drawn_coli, visual_line,
                             line->text + drawn_bytei, bytei - drawn_bytei);
      command_list_reset_color(cmdbuf->cmds);

      drawn_coli = coli;
      drawn_bytei = bytei;

      // apply new properties
      apply_properties(cmdbuf->cmds, properties, nproperties);
    }

    prev_properties_hash = new_properties_hash;
    bytei += codepoint->nbytes;
    coli += visual_char_width(codepoint, tab_width);
  }

  // flush remaining
  command_list_draw_text(cmdbuf->cmds, drawn_coli, visual_line,
                         line->text + drawn_bytei, bytei - drawn_bytei);

  drawn_coli = coli;
  drawn_bytei = bytei;

  command_list_reset_color(cmdbuf->cmds);
  command_list_set_show_whitespace(cmdbuf->cmds, false);

  // TODO: considering the whole screen is cleared, is this really needed?
  if (drawn_coli < cmdbuf->width) {
    command_list_draw_repeated(cmdbuf->cmds, drawn_coli, visual_line, ' ',
                               cmdbuf->width - drawn_coli);
  }
}

void buffer_update(struct buffer *buffer) {
  VEC_FOR_EACH(&buffer->hooks->update_hooks, struct update_hook * h) {
    h->callback(buffer, h->userdata);
  }
}

void buffer_render(struct buffer *buffer, struct buffer_render_params *params) {
  if (params->width == 0 || params->height == 0) {
    return;
  }

  VEC_FOR_EACH(&buffer->hooks->render_hooks, struct render_hook * h) {
    h->callback(buffer, h->userdata, params->origin, params->width,
                params->height);
  }

  struct setting *show_ws = settings_get("editor.show-whitespace");

  struct cmdbuf cmdbuf = (struct cmdbuf){
      .cmds = params->commands,
      .origin = params->origin,
      .width = params->width,
      .height = params->height,
      .show_ws = (show_ws != NULL ? show_ws->value.data.bool_value : true) &&
                 !buffer->force_show_ws_off,
      .buffer = buffer,
  };
  text_for_each_line(buffer->text, params->origin.line, params->height,
                     render_line, &cmdbuf);

  // draw empty lines
  uint32_t nlines = text_num_lines(buffer->text);
  for (uint32_t linei = nlines - params->origin.line; linei < params->height;
       ++linei) {
    command_list_draw_repeated(params->commands, 0, linei, ' ', params->width);
  }
}

void buffer_add_text_property(struct buffer *buffer, struct location start,
                              struct location end,
                              struct text_property property) {
  struct location bytestart = buffer_location_to_byte_coords(buffer, start);
  struct location byteend = buffer_location_to_byte_coords(buffer, end);
  text_add_property(buffer->text, bytestart.line, bytestart.col, byteend.line,
                    byteend.col, property);
}

void buffer_get_text_properties(struct buffer *buffer, struct location location,
                                struct text_property **properties,
                                uint32_t max_nproperties,
                                uint32_t *nproperties) {
  struct location bytecoords = buffer_location_to_byte_coords(buffer, location);
  text_get_properties(buffer->text, bytecoords.line, bytecoords.col, properties,
                      max_nproperties, nproperties);
}

void buffer_clear_text_properties(struct buffer *buffer) {
  text_clear_properties(buffer->text);
}

static int compare_lines(const void *l1, const void *l2) {
  return s8cmp(*(const struct s8 *)l1, *(const struct s8 *)l2);
}

void buffer_sort_lines(struct buffer *buffer, uint32_t start_line,
                       uint32_t end_line) {
  const uint32_t nlines = text_num_lines(buffer->text);
  if (nlines == 0) {
    return;
  }

  uint32_t start = start_line >= nlines ? nlines - 1 : start_line;
  uint32_t end = end_line >= nlines ? nlines - 1 : end_line;

  if (end <= start) {
    return;
  }

  const uint32_t ntosort = end - start + 1;

  struct region region =
      region_new((struct location){.line = start, .col = 0},
                 (struct location){.line = end + 1, .col = 0});

  struct s8 *lines = (struct s8 *)malloc(sizeof(struct s8) * ntosort);

  struct location bytebeg =
      buffer_location_to_byte_coords(buffer, region.begin);
  struct location byteend = buffer_location_to_byte_coords(buffer, region.end);
  struct text_chunk txt = text_get_region(
      buffer->text, bytebeg.line, bytebeg.col, byteend.line, byteend.col);

  uint32_t line_start = 0;
  uint32_t curr_line = 0;
  for (uint32_t bytei = 0; bytei < txt.nbytes; ++bytei) {
    if (txt.text[bytei] == '\n') {
      lines[curr_line] =
          (struct s8){.s = &txt.text[line_start], .l = bytei - line_start + 1};

      ++curr_line;
      line_start = bytei + 1;
    }
  }

  qsort(lines, ntosort, sizeof(struct s8), compare_lines);

  struct location at = buffer_delete(buffer, region);
  for (uint32_t linei = 0; linei < ntosort; ++linei) {
    struct s8 line = lines[linei];
    at = buffer_add(buffer, at, (uint8_t *)line.s, line.l);
  }

  // if the last line we are sorting is the last line in the buffer,
  // we have added one extra unwanted newline
  if (end == nlines - 1) {
    strip_final_newline(buffer);
  }

  if (txt.allocated) {
    free(txt.text);
  }
}

struct location buffer_location_to_byte_coords(struct buffer *buffer,
                                               struct location coords) {
  struct utf8_codepoint_iterator iter =
      text_line_codepoint_iterator(buffer->text, coords.line);
  uint32_t byteoffset = 0, col = 0, tab_width = get_tab_width(buffer);
  struct codepoint *codepoint;

  /* Let this walk up to (and including the target column) to
   * make sure we account for zero-width characters when calculating the
   * byte offset.
   */
  while (col <= coords.col &&
         (codepoint = utf8_next_codepoint(&iter)) != NULL) {
    byteoffset += codepoint->nbytes;
    col += visual_char_width(codepoint, tab_width);
  }

  /* Remove the byte-width of the last char again since it gives us the
   * position right before it while still taking zero-width codepoints
   * into account.
   */
  return (struct location){.line = coords.line,
                           .col = byteoffset -
                                  (codepoint != NULL ? codepoint->nbytes : 0)};
}

struct match_result
buffer_find_prev_in_line(struct buffer *buffer, struct location start,
                         bool (*predicate)(const struct codepoint *c)) {
  return find_prev_in_line(buffer, start, predicate);
}

struct match_result
buffer_find_next_in_line(struct buffer *buffer, struct location start,
                         bool (*predicate)(const struct codepoint *c)) {
  return find_next_in_line(buffer, start, predicate);
}

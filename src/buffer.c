#include "buffer.h"
#include "binding.h"
#include "display.h"
#include "errno.h"
#include "lang.h"
#include "minibuffer.h"
#include "reactor.h"
#include "settings.h"
#include "utf8.h"

#include <fcntl.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

struct modeline {
  uint8_t *buffer;
};

#define KILL_RING_SZ 64
static struct kill_ring {
  struct text_chunk buffer[KILL_RING_SZ];
  struct buffer_location last_paste;
  bool paste_up_to_date;
  uint32_t curr_idx;
  uint32_t paste_idx;
} g_kill_ring = {.curr_idx = 0,
                 .buffer = {0},
                 .last_paste = {0},
                 .paste_idx = 0,
                 .paste_up_to_date = false};

struct update_hook_result buffer_linenum_hook(struct buffer *buffer,
                                              struct command_list *commands,
                                              uint32_t width, uint32_t height,
                                              uint64_t frame_time,
                                              void *userdata);

struct update_hook_result buffer_modeline_hook(struct buffer *buffer,
                                               struct command_list *commands,
                                               uint32_t width, uint32_t height,
                                               uint64_t frame_time,
                                               void *userdata);

struct buffer buffer_create(char *name, bool modeline) {
  struct buffer b = (struct buffer){
      .filename = NULL,
      .name = strdup(name),
      .text = text_create(10),
      .dot = {0},
      .mark = {0},
      .mark_set = false,
      .modified = false,
      .readonly = false,
      .modeline = NULL,
      .keymaps = calloc(10, sizeof(struct keymap)),
      .nkeymaps = 1,
      .scroll = {0},
      .update_hooks = {0},
      .nkeymaps_max = 10,
      .lang = lang_from_id("fnd"),
  };

  undo_init(&b.undo, 100);

  b.keymaps[0] = keymap_create("buffer-default", 128);
  struct binding bindings[] = {
      BINDING(Ctrl, 'B', "backward-char"),
      BINDING(LEFT, "backward-char"),
      BINDING(Ctrl, 'F', "forward-char"),
      BINDING(RIGHT, "forward-char"),

      BINDING(Ctrl, 'P', "backward-line"),
      BINDING(UP, "backward-line"),
      BINDING(Ctrl, 'N', "forward-line"),
      BINDING(DOWN, "forward-line"),

      BINDING(Meta, 'f', "forward-word"),
      BINDING(Meta, 'b', "backward-word"),

      BINDING(Ctrl, 'A', "beginning-of-line"),
      BINDING(Ctrl, 'E', "end-of-line"),

      BINDING(Meta, '<', "goto-beginning"),
      BINDING(Meta, '>', "goto-end"),

      BINDING(ENTER, "newline"),
      BINDING(TAB, "indent"),

      BINDING(Ctrl, 'K', "kill-line"),
      BINDING(DELETE, "delete-char"),
      BINDING(Ctrl, 'D', "delete-char"),
      BINDING(BACKSPACE, "backward-delete-char"),

      BINDING(Ctrl, '@', "set-mark"),

      BINDING(Ctrl, 'W', "cut"),
      BINDING(Ctrl, 'Y', "paste"),
      BINDING(Meta, 'y', "paste-older"),
      BINDING(Meta, 'w', "copy"),

      BINDING(Ctrl, '_', "undo"),
  };
  keymap_bind_keys(&b.keymaps[0], bindings,
                   sizeof(bindings) / sizeof(bindings[0]));

  if (modeline) {
    b.modeline = calloc(1, sizeof(struct modeline));
    b.modeline->buffer = malloc(1024);
    b.modeline->buffer[0] = '\0';
    buffer_add_update_hook(&b, buffer_modeline_hook, b.modeline);
  }

  if (modeline) {
    buffer_add_update_hook(&b, buffer_linenum_hook, NULL);
  }

  return b;
}

void buffer_destroy(struct buffer *buffer) {
  text_destroy(buffer->text);
  buffer->text = NULL;

  free(buffer->name);
  buffer->name = NULL;

  free(buffer->filename);
  buffer->filename = NULL;

  for (uint32_t keymapi = 0; keymapi < buffer->nkeymaps; ++keymapi) {
    keymap_destroy(&buffer->keymaps[keymapi]);
  }
  free(buffer->keymaps);
  buffer->keymaps = NULL;

  if (buffer->modeline != NULL) {
    free(buffer->modeline->buffer);
    free(buffer->modeline);
  }

  undo_destroy(&buffer->undo);
}

void buffer_clear(struct buffer *buffer) {
  text_clear(buffer->text);
  buffer->dot.col = buffer->dot.line = 0;
}

void buffer_static_init() {
  settings_register_setting(
      "editor.tab-width",
      (struct setting_value){.type = Setting_Number, .number_value = 4});

  settings_register_setting(
      "editor.show-whitespace",
      (struct setting_value){.type = Setting_Bool, .bool_value = true});
}

void buffer_static_teardown() {
  for (uint32_t i = 0; i < KILL_RING_SZ; ++i) {
    if (g_kill_ring.buffer[i].allocated) {
      free(g_kill_ring.buffer[i].text);
    }
  }
}

bool buffer_is_empty(struct buffer *buffer) {
  return text_num_lines(buffer->text) == 0;
}

bool buffer_is_modified(struct buffer *buffer) { return buffer->modified; }

bool buffer_is_readonly(struct buffer *buffer) { return buffer->readonly; }
void buffer_set_readonly(struct buffer *buffer, bool readonly) {
  buffer->readonly = readonly;
}

void delete_with_undo(struct buffer *buffer, struct buffer_location start,
                      struct buffer_location end) {
  if (buffer->readonly) {
    minibuffer_echo_timeout(4, "buffer is read-only");
    return;
  }

  struct text_chunk txt =
      text_get_region(buffer->text, start.line, start.col, end.line, end.col);

  undo_push_delete(
      &buffer->undo,
      (struct undo_delete){.data = txt.text,
                           .nbytes = txt.nbytes,
                           .pos = {.row = start.line, .col = start.col}});
  undo_push_boundary(&buffer->undo,
                     (struct undo_boundary){.save_point = false});

  text_delete(buffer->text, start.line, start.col, end.line, end.col);
  buffer->modified = true;
}

uint32_t buffer_keymaps(struct buffer *buffer, struct keymap **keymaps_out) {
  *keymaps_out = buffer->keymaps;
  return buffer->nkeymaps;
}

void buffer_add_keymap(struct buffer *buffer, struct keymap *keymap) {
  if (buffer->nkeymaps == buffer->nkeymaps_max) {
    buffer->nkeymaps_max *= 2;
    buffer->keymaps =
        realloc(buffer->keymaps, sizeof(struct keymap) * buffer->nkeymaps_max);
  }
  buffer->keymaps[buffer->nkeymaps] = *keymap;
  ++buffer->nkeymaps;
}

void buffer_goto_beginning(struct buffer *buffer) {
  buffer->dot.col = 0;
  buffer->dot.line = 0;
}

void buffer_goto_end(struct buffer *buffer) {
  buffer->dot.line = text_num_lines(buffer->text);
  buffer->dot.col = 0;
}

bool movev(struct buffer *buffer, int rowdelta) {
  int64_t new_line = (int64_t)buffer->dot.line + rowdelta;

  if (new_line < 0) {
    buffer->dot.line = 0;
    return false;
  } else if (new_line > text_num_lines(buffer->text)) {
    buffer->dot.line = text_num_lines(buffer->text);
    return false;
  } else {
    buffer->dot.line = (uint32_t)new_line;

    // make sure column stays on the line
    uint32_t linelen = text_line_length(buffer->text, buffer->dot.line);
    buffer->dot.col = buffer->dot.col > linelen ? linelen : buffer->dot.col;
    return true;
  }
}

// move dot `coldelta` chars
bool moveh(struct buffer *buffer, int coldelta) {
  int64_t new_col = (int64_t)buffer->dot.col + coldelta;

  if (new_col > (int64_t)text_line_length(buffer->text, buffer->dot.line)) {
    if (movev(buffer, 1)) {
      buffer->dot.col = 0;
    }
  } else if (new_col < 0) {
    if (movev(buffer, -1)) {
      buffer->dot.col = text_line_length(buffer->text, buffer->dot.line);
    } else {
      return false;
    }
  } else {
    buffer->dot.col = new_col;
  }

  return true;
}

void buffer_goto(struct buffer *buffer, uint32_t line, uint32_t col) {
  int64_t linedelta = (int64_t)line - (int64_t)buffer->dot.line;
  movev(buffer, linedelta);

  int64_t coldelta = (int64_t)col - (int64_t)buffer->dot.col;
  moveh(buffer, coldelta);
}

struct region {
  struct buffer_location begin;
  struct buffer_location end;
};

struct region to_region(struct buffer_location dot,
                        struct buffer_location mark) {
  struct region reg = {.begin = mark, .end = dot};

  if (dot.line < mark.line || (dot.line == mark.line && dot.col < mark.col)) {
    reg.begin = dot;
    reg.end = mark;
  }

  return reg;
}

struct region buffer_get_region(struct buffer *buffer) {
  return to_region(buffer->dot, buffer->mark);
}

bool buffer_region_has_size(struct buffer *buffer) {
  return buffer->mark_set &&
         (labs((int64_t)buffer->mark.line - (int64_t)buffer->dot.line) +
          labs((int64_t)buffer->mark.col - (int64_t)buffer->dot.col)) > 0;
}

struct text_chunk *copy_region(struct buffer *buffer, struct region region) {
  struct text_chunk *curr = &g_kill_ring.buffer[g_kill_ring.curr_idx];
  g_kill_ring.curr_idx = g_kill_ring.curr_idx + 1 % KILL_RING_SZ;

  if (curr->allocated) {
    free(curr->text);
  }

  struct text_chunk txt =
      text_get_region(buffer->text, region.begin.line, region.begin.col,
                      region.end.line, region.end.col);
  *curr = txt;
  return curr;
}

void buffer_copy(struct buffer *buffer) {
  if (buffer_region_has_size(buffer)) {
    struct region reg = buffer_get_region(buffer);
    struct text_chunk *curr = copy_region(buffer, reg);
    buffer_clear_mark(buffer);
  }
}

void paste(struct buffer *buffer, uint32_t ring_idx) {
  if (ring_idx > 0) {
    struct text_chunk *curr = &g_kill_ring.buffer[ring_idx - 1];
    if (curr->text != NULL) {
      g_kill_ring.last_paste = buffer->mark_set ? buffer->mark : buffer->dot;
      buffer_add_text(buffer, curr->text, curr->nbytes);
      g_kill_ring.paste_up_to_date = true;
    }
  }
}

void buffer_paste(struct buffer *buffer) {
  g_kill_ring.paste_idx = g_kill_ring.curr_idx;
  paste(buffer, g_kill_ring.curr_idx);
}

void buffer_paste_older(struct buffer *buffer) {
  if (g_kill_ring.paste_up_to_date) {

    // remove previous paste
    struct text_chunk *curr = &g_kill_ring.buffer[g_kill_ring.curr_idx];
    delete_with_undo(buffer, g_kill_ring.last_paste, buffer->dot);

    // place ourselves right
    buffer->dot = g_kill_ring.last_paste;

    // paste older
    if (g_kill_ring.paste_idx - 1 > 0) {
      --g_kill_ring.paste_idx;
    } else {
      g_kill_ring.paste_idx = g_kill_ring.curr_idx;
    }

    paste(buffer, g_kill_ring.paste_idx);

  } else {
    buffer_paste(buffer);
  }
}

void buffer_cut(struct buffer *buffer) {
  if (buffer_region_has_size(buffer)) {
    struct region reg = buffer_get_region(buffer);
    copy_region(buffer, reg);
    delete_with_undo(buffer, reg.begin, reg.end);
    buffer_clear_mark(buffer);
    buffer->dot = reg.begin;
  }
}

bool maybe_delete_region(struct buffer *buffer) {
  if (buffer_region_has_size(buffer)) {
    struct region reg = buffer_get_region(buffer);
    delete_with_undo(buffer, reg.begin, reg.end);
    buffer_clear_mark(buffer);
    buffer->dot = reg.begin;
    return true;
  }

  return false;
}

void buffer_kill_line(struct buffer *buffer) {
  uint32_t nchars =
      text_line_length(buffer->text, buffer->dot.line) - buffer->dot.col;
  if (nchars == 0) {
    nchars = 1;
  }

  struct region reg = {
      .begin = buffer->dot,
      .end =
          {
              .line = buffer->dot.line,
              .col = buffer->dot.col + nchars,
          },
  };
  copy_region(buffer, reg);
  delete_with_undo(buffer, buffer->dot,
                   (struct buffer_location){
                       .line = buffer->dot.line,
                       .col = buffer->dot.col + nchars,
                   });
}

void buffer_forward_delete_char(struct buffer *buffer) {
  if (maybe_delete_region(buffer)) {
    return;
  }

  delete_with_undo(buffer, buffer->dot,
                   (struct buffer_location){
                       .line = buffer->dot.line,
                       .col = buffer->dot.col + 1,
                   });
}

void buffer_backward_delete_char(struct buffer *buffer) {
  if (maybe_delete_region(buffer)) {
    return;
  }

  if (moveh(buffer, -1)) {
    buffer_forward_delete_char(buffer);
  }
}

void buffer_backward_char(struct buffer *buffer) { moveh(buffer, -1); }
void buffer_forward_char(struct buffer *buffer) { moveh(buffer, 1); }

struct buffer_location find_next(struct buffer *buffer, uint8_t chars[],
                                 uint32_t nchars, int direction) {
  struct text_chunk line = text_get_line(buffer->text, buffer->dot.line);
  int64_t bytei =
      text_col_to_byteindex(buffer->text, buffer->dot.line, buffer->dot.col);
  while (bytei < line.nbytes && bytei > 0 &&
         (line.text[bytei] == ' ' || line.text[bytei] == '.')) {
    bytei += direction;
  }

  for (; bytei < line.nbytes && bytei > 0; bytei += direction) {
    uint8_t b = line.text[bytei];
    if (b == ' ' || b == '.') {
      break;
    }
  }

  uint32_t target_col =
      text_byteindex_to_col(buffer->text, buffer->dot.line, bytei);
  return (struct buffer_location){.line = buffer->dot.line, .col = target_col};
}

void buffer_forward_word(struct buffer *buffer) {
  moveh(buffer, 1);
  uint8_t chars[] = {' ', '.'};
  buffer->dot = find_next(buffer, chars, 2, 1);
}

void buffer_backward_word(struct buffer *buffer) {
  moveh(buffer, -1);
  uint8_t chars[] = {' ', '.'};
  buffer->dot = find_next(buffer, chars, 2, -1);
}

void buffer_backward_line(struct buffer *buffer) { movev(buffer, -1); }
void buffer_forward_line(struct buffer *buffer) { movev(buffer, 1); }

void buffer_end_of_line(struct buffer *buffer) {
  buffer->dot.col = text_line_length(buffer->text, buffer->dot.line);
}

void buffer_beginning_of_line(struct buffer *buffer) { buffer->dot.col = 0; }

struct buffer buffer_from_file(char *filename) {
  struct buffer b = buffer_create(basename((char *)filename), true);
  b.filename = strdup(filename);
  if (access(b.filename, F_OK) == 0) {
    FILE *file = fopen(filename, "r");

    if (file == NULL) {
      minibuffer_echo("Error opening %s: %s", filename, strerror(errno));
      return b;
    }

    while (true) {
      uint8_t buff[4096];
      int bytes = fread(buff, 1, 4096, file);
      if (bytes > 0) {
        uint32_t ignore;
        text_append(b.text, buff, bytes, &ignore, &ignore);
      } else if (bytes == 0) {
        break; // EOF
      } else {
        minibuffer_echo("error reading from %s: %s", filename, strerror(errno));
        fclose(file);
        return b;
      }
    }

    fclose(file);
  }

  const char *ext = strrchr(b.filename, '.');
  if (ext != NULL) {
    b.lang = lang_from_extension(ext + 1);
  }
  undo_push_boundary(&b.undo, (struct undo_boundary){.save_point = true});
  return b;
}

void write_line(struct text_chunk *chunk, void *userdata) {
  FILE *file = (FILE *)userdata;
  fwrite(chunk->text, 1, chunk->nbytes, file);

  // final newline is not optional!
  fputc('\n', file);
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

  FILE *file = fopen(buffer->filename, "w");
  if (file == NULL) {
    minibuffer_echo("failed to open file %s for writing: %s", buffer->filename,
                    strerror(errno));
    return;
  }

  uint32_t nlines = text_num_lines(buffer->text);
  struct text_chunk lastline = text_get_line(buffer->text, nlines - 1);
  uint32_t nlines_to_write = lastline.nbytes == 0 ? nlines - 1 : nlines;

  text_for_each_line(buffer->text, 0, nlines_to_write, write_line, file);
  minibuffer_echo_timeout(4, "wrote %d lines to %s", nlines_to_write,
                          buffer->filename);
  fclose(file);

  buffer->modified = false;
  undo_push_boundary(&buffer->undo, (struct undo_boundary){.save_point = true});
}

void buffer_write_to(struct buffer *buffer, const char *filename) {
  buffer->filename = strdup(filename);
  buffer_to_file(buffer);
}

int buffer_add_text(struct buffer *buffer, uint8_t *text, uint32_t nbytes) {
  if (buffer->readonly) {
    minibuffer_echo_timeout(4, "buffer is read-only");
    return 0;
  }

  // invalidate last paste
  g_kill_ring.paste_up_to_date = false;

  /* If we currently have a selection active,
   * replace it with the text to insert. */
  maybe_delete_region(buffer);

  struct buffer_location initial = buffer->dot;

  uint32_t lines_added, cols_added;
  text_insert_at(buffer->text, initial.line, initial.col, text, nbytes,
                 &lines_added, &cols_added);

  // move to after inserted text
  movev(buffer, lines_added);
  if (lines_added > 0) {
    // does not make sense to use position from another line
    buffer->dot.col = 0;
  }
  moveh(buffer, cols_added);

  struct buffer_location final = buffer->dot;
  undo_push_add(
      &buffer->undo,
      (struct undo_add){.begin = {.row = initial.line, .col = initial.col},
                        .end = {.row = final.line, .col = final.col}});

  if (lines_added > 0) {
    undo_push_boundary(&buffer->undo,
                       (struct undo_boundary){.save_point = false});
  }

  buffer->modified = true;
  return lines_added;
}

void buffer_newline(struct buffer *buffer) {
  buffer_add_text(buffer, (uint8_t *)"\n", 1);
}

void buffer_indent(struct buffer *buffer) {
  uint32_t tab_width = buffer->lang.tab_width;
  buffer_add_text(buffer, (uint8_t *)"                ",
                  tab_width > 16 ? 16 : tab_width);
}

uint32_t buffer_add_update_hook(struct buffer *buffer, update_hook_cb hook,
                                void *userdata) {
  struct update_hook *h =
      &buffer->update_hooks.hooks[buffer->update_hooks.nhooks];
  h->callback = hook;
  h->userdata = userdata;

  ++buffer->update_hooks.nhooks;

  // TODO: cant really have this if we actually want to remove a hook
  return buffer->update_hooks.nhooks - 1;
}

void buffer_set_mark(struct buffer *buffer) {
  buffer->mark_set
      ? buffer_clear_mark(buffer)
      : buffer_set_mark_at(buffer, buffer->dot.line, buffer->dot.col);
}

void buffer_clear_mark(struct buffer *buffer) {
  buffer->mark_set = false;
  minibuffer_echo_timeout(2, "mark cleared");
}

void buffer_set_mark_at(struct buffer *buffer, uint32_t line, uint32_t col) {
  buffer->mark_set = true;
  buffer->mark.line = line;
  buffer->mark.col = col;
  minibuffer_echo_timeout(2, "mark set");
}

void buffer_undo(struct buffer *buffer) {
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

  undo_push_boundary(undo, (struct undo_boundary){.save_point = false});
  for (uint32_t reci = 0; reci < nrecords; ++reci) {
    struct undo_record *rec = &records[reci];
    switch (rec->type) {
    case Undo_Boundary: {
      struct undo_boundary *b = &rec->boundary;
      if (b->save_point) {
        buffer->modified = false;
      }
      break;
    }
    case Undo_Add: {
      struct undo_add *add = &rec->add;

      delete_with_undo(buffer,
                       (struct buffer_location){
                           .line = add->begin.row,
                           .col = add->begin.col,
                       },
                       (struct buffer_location){
                           .line = add->end.row,
                           .col = add->end.col,
                       });

      buffer_goto(buffer, add->begin.row, add->begin.col);
      break;
    }
    case Undo_Delete: {
      struct undo_delete *del = &rec->delete;
      buffer_goto(buffer, del->pos.row, del->pos.col);
      buffer_add_text(buffer, del->data, del->nbytes);
      break;
    }
    }
  }
  undo_push_boundary(undo, (struct undo_boundary){.save_point = false});

  free(records);
  undo_end(undo);
}

struct cmdbuf {
  struct command_list *cmds;
  struct buffer_location scroll;
  uint32_t line_offset;
  uint32_t left_margin;
  uint32_t width;

  struct region region;
  bool mark_set;

  bool show_ws;

  struct line_render_hook *line_render_hooks;
  uint32_t nlinerender_hooks;
};

void render_line(struct text_chunk *line, void *userdata) {
  struct cmdbuf *cmdbuf = (struct cmdbuf *)userdata;
  uint32_t visual_line = line->line - cmdbuf->scroll.line + cmdbuf->line_offset;

  for (uint32_t hooki = 0; hooki < cmdbuf->nlinerender_hooks; ++hooki) {
    struct line_render_hook *hook = &cmdbuf->line_render_hooks[hooki];
    hook->callback(line, visual_line, cmdbuf->cmds, hook->userdata);
  }

  uint32_t scroll_bytes =
      utf8_nbytes(line->text, line->nbytes, cmdbuf->scroll.col);
  uint8_t *text = line->text + scroll_bytes;
  uint32_t text_nbytes =
      scroll_bytes > line->nbytes ? 0 : line->nbytes - scroll_bytes;
  uint32_t text_nchars =
      scroll_bytes > line->nchars ? 0 : line->nchars - cmdbuf->scroll.col;

  command_list_set_show_whitespace(cmdbuf->cmds, cmdbuf->show_ws);
  struct buffer_location *begin = &cmdbuf->region.begin,
                         *end = &cmdbuf->region.end;

  // should we draw region
  if (cmdbuf->mark_set && line->line >= begin->line &&
      line->line <= end->line) {
    uint32_t byte_offset = 0;
    uint32_t col_offset = 0;

    // draw any text on the line that should not be part of region
    if (begin->line == line->line) {
      if (begin->col > cmdbuf->scroll.col) {
        uint32_t nbytes =
            utf8_nbytes(text, text_nbytes, begin->col - cmdbuf->scroll.col);
        command_list_draw_text(cmdbuf->cmds, cmdbuf->left_margin, visual_line,
                               text, nbytes);

        byte_offset += nbytes;
      }

      col_offset = begin->col - cmdbuf->scroll.col;
    }

    // activate region color
    command_list_set_index_color_bg(cmdbuf->cmds, 5);

    // draw any text on line that should be part of region
    if (end->line == line->line) {
      if (end->col > cmdbuf->scroll.col) {
        uint32_t nbytes =
            utf8_nbytes(text + byte_offset, text_nbytes - byte_offset,
                        end->col - col_offset - cmdbuf->scroll.col);
        command_list_draw_text(cmdbuf->cmds, cmdbuf->left_margin + col_offset,
                               visual_line, text + byte_offset, nbytes);
        byte_offset += nbytes;
      }

      col_offset = end->col - cmdbuf->scroll.col;
      command_list_reset_color(cmdbuf->cmds);
    }

    // draw rest of line
    if (text_nbytes - byte_offset > 0) {
      command_list_draw_text(cmdbuf->cmds, cmdbuf->left_margin + col_offset,
                             visual_line, text + byte_offset,
                             text_nbytes - byte_offset);
    }

    // done rendering region
    command_list_reset_color(cmdbuf->cmds);

  } else {
    command_list_draw_text(cmdbuf->cmds, cmdbuf->left_margin, visual_line, text,
                           text_nbytes);
  }

  command_list_set_show_whitespace(cmdbuf->cmds, false);

  uint32_t col = text_nchars + cmdbuf->left_margin;
  for (uint32_t bytei = scroll_bytes; bytei < line->nbytes; ++bytei) {
    if (line->text[bytei] == '\t') {
      col += 3;
    } else if (utf8_byte_is_unicode_start(line->text[bytei])) {
      wchar_t wc;
      if (mbrtowc(&wc, (char *)line->text + bytei, 6, NULL) >= 0) {
        col += wcwidth(wc) - 1;
      }
    }
  }

  if (col < cmdbuf->width) {
    command_list_draw_repeated(cmdbuf->cmds, col, visual_line, ' ',
                               cmdbuf->width - col);
  }
}

void scroll(struct buffer *buffer, int line_delta, int col_delta) {
  uint32_t nlines = text_num_lines(buffer->text);
  int64_t new_line = (int64_t)buffer->scroll.line + line_delta;
  if (new_line >= 0 && new_line < nlines) {
    buffer->scroll.line = (uint32_t)new_line;
  } else if (new_line < 0) {
    buffer->scroll.line = 0;
  }

  int64_t new_col = (int64_t)buffer->scroll.col + col_delta;
  if (new_col >= 0 &&
      new_col < text_line_length(buffer->text, buffer->dot.line)) {
    buffer->scroll.col = (uint32_t)new_col;
  } else if (new_col < 0) {
    buffer->scroll.col = 0;
  }
}

void to_relative(struct buffer *buffer, uint32_t line, uint32_t col,
                 int64_t *rel_line, int64_t *rel_col) {
  *rel_col = (int64_t)col - (int64_t)buffer->scroll.col;
  *rel_line = (int64_t)line - (int64_t)buffer->scroll.line;
}

uint32_t visual_dot_col(struct buffer *buffer, uint32_t dot_col) {
  uint32_t visual_dot_col = dot_col;
  struct text_chunk line = text_get_line(buffer->text, buffer->dot.line);
  for (uint32_t bytei = 0;
       bytei <
       text_col_to_byteindex(buffer->text, buffer->dot.line, buffer->dot.col);
       ++bytei) {
    if (line.text[bytei] == '\t') {
      visual_dot_col += 3;
    } else if (utf8_byte_is_unicode_start(line.text[bytei])) {
      wchar_t wc;
      if (mbrtowc(&wc, (char *)line.text + bytei, 6, NULL) >= 0) {
        visual_dot_col += wcwidth(wc) - 1;
      }
    }
  }

  return visual_dot_col;
}

struct update_hook_result buffer_modeline_hook(struct buffer *buffer,
                                               struct command_list *commands,
                                               uint32_t width, uint32_t height,
                                               uint64_t frame_time,
                                               void *userdata) {
  char buf[width * 4];

  static uint64_t samples[10] = {0};
  static uint32_t samplei = 0;
  static uint64_t avg = 0;

  // calc a moving average with a window of the last 10 frames
  ++samplei;
  samplei %= 10;
  avg += 0.1 * (frame_time - samples[samplei]);
  samples[samplei] = frame_time;

  time_t now = time(NULL);
  struct tm *lt = localtime(&now);
  char left[128], right[128];

  snprintf(left, 128, "  %c%c %-16s (%d, %d) (%s)",
           buffer->modified ? '*' : '-', buffer->readonly ? '%' : '-',
           buffer->name, buffer->dot.line + 1,
           visual_dot_col(buffer, buffer->dot.col), buffer->lang.name);
  snprintf(right, 128, "(%.2f ms) %02d:%02d", frame_time / 1e6, lt->tm_hour,
           lt->tm_min);

  snprintf(buf, width * 4, "%s%*s%s", left,
           (int)(width - (strlen(left) + strlen(right))), "", right);

  struct modeline *modeline = (struct modeline *)userdata;
  if (strcmp(buf, (char *)modeline->buffer) != 0) {
    modeline->buffer = realloc(modeline->buffer, width * 4);
    strcpy((char *)modeline->buffer, buf);
  }

  command_list_set_index_color_bg(commands, 8);
  command_list_draw_text(commands, 0, height - 1, modeline->buffer,
                         strlen((char *)modeline->buffer));
  command_list_reset_color(commands);

  struct update_hook_result res = {0};
  res.margins.bottom = 1;
  return res;
}

struct linenumdata {
  uint32_t longest_nchars;
  uint32_t dot_line;
} linenum_data;

void linenum_render_hook(struct text_chunk *line_data, uint32_t line,
                         struct command_list *commands, void *userdata) {
  struct linenumdata *data = (struct linenumdata *)userdata;
  static char buf[16];
  command_list_set_index_color_bg(commands, 236);
  command_list_set_index_color_fg(
      commands, line_data->line == data->dot_line ? 253 : 244);
  uint32_t chars =
      snprintf(buf, 16, "%*d", data->longest_nchars + 1, line_data->line + 1);
  command_list_draw_text_copy(commands, 0, line, (uint8_t *)buf, chars);
  command_list_reset_color(commands);
  command_list_draw_text(commands, data->longest_nchars + 1, line,
                         (uint8_t *)" ", 1);
}

void clear_empty_linenum_lines(uint32_t line, struct command_list *commands,
                               void *userdata) {
  struct linenumdata *data = (struct linenumdata *)userdata;
  uint32_t longest_nchars = data->longest_nchars;
  command_list_draw_repeated(commands, 0, line, ' ', longest_nchars + 2);
}

struct update_hook_result buffer_linenum_hook(struct buffer *buffer,
                                              struct command_list *commands,
                                              uint32_t width, uint32_t height,
                                              uint64_t frame_time,
                                              void *userdata) {
  uint32_t total_lines = text_num_lines(buffer->text);
  uint32_t longest_nchars = 10;
  if (total_lines < 10) {
    longest_nchars = 1;
  } else if (total_lines < 100) {
    longest_nchars = 2;
  } else if (total_lines < 1000) {
    longest_nchars = 3;
  } else if (total_lines < 10000) {
    longest_nchars = 4;
  } else if (total_lines < 100000) {
    longest_nchars = 5;
  } else if (total_lines < 1000000) {
    longest_nchars = 6;
  } else if (total_lines < 10000000) {
    longest_nchars = 7;
  } else if (total_lines < 100000000) {
    longest_nchars = 8;
  } else if (total_lines < 1000000000) {
    longest_nchars = 9;
  }

  linenum_data.longest_nchars = longest_nchars;
  linenum_data.dot_line = buffer->dot.line;
  struct update_hook_result res = {0};
  res.margins.left = longest_nchars + 2;
  res.line_render_hook.callback = linenum_render_hook;
  res.line_render_hook.empty_callback = clear_empty_linenum_lines;
  res.line_render_hook.userdata = &linenum_data;

  return res;
}

void buffer_update(struct buffer *buffer, uint32_t width, uint32_t height,
                   struct command_list *commands, uint64_t frame_time,
                   uint32_t *relline, uint32_t *relcol) {
  if (width == 0 || height == 0) {
    return;
  }

  uint32_t total_width = width, total_height = height;
  struct margin total_margins = {0};
  struct line_render_hook line_hooks[16];
  uint32_t nlinehooks = 0;
  for (uint32_t hooki = 0; hooki < buffer->update_hooks.nhooks; ++hooki) {
    struct update_hook *h = &buffer->update_hooks.hooks[hooki];
    struct update_hook_result res =
        h->callback(buffer, commands, width, height, frame_time, h->userdata);

    struct margin margins = res.margins;

    if (res.line_render_hook.callback != NULL) {
      line_hooks[nlinehooks] = res.line_render_hook;
      ++nlinehooks;
    }

    total_margins.left += margins.left;
    total_margins.right += margins.right;
    total_margins.top += margins.top;
    total_margins.bottom += margins.bottom;

    height -= margins.top + margins.bottom;
    width -= margins.left + margins.right;
  }

  int64_t rel_line, rel_col;
  to_relative(buffer, buffer->dot.line, buffer->dot.col, &rel_line, &rel_col);
  int line_delta = 0, col_delta = 0;
  if (rel_line < 0) {
    line_delta = rel_line - ((int)height / 2);
  } else if (rel_line >= height) {
    line_delta = (rel_line - height) + height / 2;
  }

  if (rel_col < 0) {
    col_delta = rel_col - ((int)width / 2);
  } else if (rel_col > width) {
    col_delta = (rel_col - width) + width / 2;
  }

  scroll(buffer, line_delta, col_delta);

  struct setting *show_ws = settings_get("editor.show-whitespace");

  struct cmdbuf cmdbuf = (struct cmdbuf){
      .cmds = commands,
      .scroll = buffer->scroll,
      .left_margin = total_margins.left,
      .width = total_width,
      .line_offset = total_margins.top,
      .line_render_hooks = line_hooks,
      .nlinerender_hooks = nlinehooks,
      .mark_set = buffer->mark_set,
      .region = to_region(buffer->dot, buffer->mark),
      .show_ws = show_ws != NULL ? show_ws->value.bool_value : true,
  };
  text_for_each_line(buffer->text, buffer->scroll.line, height, render_line,
                     &cmdbuf);

  // draw empty lines
  uint32_t nlines = text_num_lines(buffer->text);
  for (uint32_t linei = nlines - buffer->scroll.line + total_margins.top;
       linei < height; ++linei) {

    for (uint32_t hooki = 0; hooki < nlinehooks; ++hooki) {
      struct line_render_hook *hook = &line_hooks[hooki];
      hook->empty_callback(linei, commands, hook->userdata);
    }

    command_list_draw_repeated(commands, total_margins.left, linei, ' ',
                               total_width - total_margins.left);
  }

  // update the visual cursor position
  to_relative(buffer, buffer->dot.line, buffer->dot.col, &rel_line, &rel_col);
  uint32_t visual_col = visual_dot_col(buffer, buffer->dot.col);
  to_relative(buffer, buffer->dot.line, visual_col, &rel_line, &rel_col);

  *relline = rel_line < 0 ? 0 : (uint32_t)rel_line + total_margins.top;
  *relcol = rel_col < 0 ? 0 : (uint32_t)rel_col + total_margins.left;
}

struct text_chunk buffer_get_line(struct buffer *buffer, uint32_t line) {
  return text_get_line(buffer->text, line);
}

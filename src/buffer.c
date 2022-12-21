#include "buffer.h"
#include "binding.h"
#include "display.h"
#include "minibuffer.h"
#include "reactor.h"

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct buffer buffer_create(const char *name, bool modeline) {
  struct buffer b =
      (struct buffer){.filename = NULL,
                      .name = name,
                      .text = text_create(10),
                      .dot_col = 0,
                      .dot_line = 0,
                      .keymaps = calloc(10, sizeof(struct keymap)),
                      .nkeymaps = 1,
                      .scroll_col = 0,
                      .scroll_line = 0,
                      .update_hooks = {0},
                      .nkeymaps_max = 10};

  b.keymaps[0] = keymap_create("buffer-default", 128);
  struct binding bindings[] = {
      BINDING(Ctrl, 'B', "backward-char"),
      BINDING(Meta, 'D', "backward-char"),
      BINDING(Ctrl, 'F', "forward-char"),
      BINDING(Meta, 'C', "forward-char"),

      BINDING(Ctrl, 'P', "backward-line"),
      BINDING(Meta, 'A', "backward-line"),
      BINDING(Ctrl, 'N', "forward-line"),
      BINDING(Meta, 'B', "forward-line"),

      BINDING(Ctrl, 'A', "beginning-of-line"),
      BINDING(Ctrl, 'E', "end-of-line"),

      BINDING(Ctrl, 'M', "newline"),

      BINDING(Ctrl, 'K', "kill-line"),
      BINDING(Meta, '~', "delete-char"),
      BINDING(Ctrl, '?', "backward-delete-char"),
  };
  keymap_bind_keys(&b.keymaps[0], bindings,
                   sizeof(bindings) / sizeof(bindings[0]));

  if (modeline) {
    struct modeline *modeline = calloc(1, sizeof(struct modeline));
    modeline->buffer = malloc(1024);
    buffer_add_update_hook(&b, buffer_modeline_hook, modeline);
  }

  return b;
}

void buffer_destroy(struct buffer *buffer) {
  text_destroy(buffer->text);
  free(buffer->text);
}

void buffer_clear(struct buffer *buffer) {
  text_clear(buffer->text);
  buffer->dot_col = buffer->dot_line = 0;
}

bool buffer_is_empty(struct buffer *buffer) {
  return text_num_lines(buffer->text) == 0 &&
         text_line_size(buffer->text, 0) == 0;
}

uint32_t buffer_keymaps(struct buffer *buffer, struct keymap **keymaps_out) {
  *keymaps_out = buffer->keymaps;
  return buffer->nkeymaps;
}

void buffer_add_keymap(struct buffer *buffer, struct keymap *keymap) {
  if (buffer->nkeymaps == buffer->nkeymaps_max) {
    // TODO: better
    return;
  }
  buffer->keymaps[buffer->nkeymaps] = *keymap;
  ++buffer->nkeymaps;
}

bool movev(struct buffer *buffer, int rowdelta) {
  int64_t new_line = (int64_t)buffer->dot_line + rowdelta;

  if (new_line < 0) {
    buffer->dot_line = 0;
    return false;
  } else if (new_line > text_num_lines(buffer->text) - 1) {
    buffer->dot_line = text_num_lines(buffer->text) - 1;
    return false;
  } else {
    buffer->dot_line = (uint32_t)new_line;

    // make sure column stays on the line
    uint32_t linelen = text_line_length(buffer->text, buffer->dot_line);
    buffer->dot_col = buffer->dot_col > linelen ? linelen : buffer->dot_col;
    return true;
  }
}

// move dot `coldelta` chars
void moveh(struct buffer *buffer, int coldelta) {
  int64_t new_col = (int64_t)buffer->dot_col + coldelta;

  if (new_col > (int64_t)text_line_length(buffer->text, buffer->dot_line)) {
    if (movev(buffer, 1)) {
      buffer->dot_col = 0;
    }
  } else if (new_col < 0) {
    if (movev(buffer, -1)) {
      buffer->dot_col = text_line_length(buffer->text, buffer->dot_line);
    }
  } else {
    buffer->dot_col = new_col;
  }
}

void buffer_kill_line(struct buffer *buffer) {
  uint32_t nchars =
      text_line_length(buffer->text, buffer->dot_line) - buffer->dot_col;
  if (nchars == 0) {
    nchars = 1;
  }

  text_delete(buffer->text, buffer->dot_line, buffer->dot_col, nchars);
}

void buffer_forward_delete_char(struct buffer *buffer) {
  text_delete(buffer->text, buffer->dot_line, buffer->dot_col, 1);
}

void buffer_backward_delete_char(struct buffer *buffer) {
  moveh(buffer, -1);
  buffer_forward_delete_char(buffer);
}

void buffer_backward_char(struct buffer *buffer) { moveh(buffer, -1); }
void buffer_forward_char(struct buffer *buffer) { moveh(buffer, 1); }

void buffer_backward_line(struct buffer *buffer) { movev(buffer, -1); }
void buffer_forward_line(struct buffer *buffer) { movev(buffer, 1); }

void buffer_end_of_line(struct buffer *buffer) {
  buffer->dot_col = text_line_length(buffer->text, buffer->dot_line);
}

void buffer_beginning_of_line(struct buffer *buffer) { buffer->dot_col = 0; }

struct buffer buffer_from_file(const char *filename, struct reactor *reactor) {
  struct buffer b = buffer_create(filename, true);
  b.filename = filename;
  if (access(b.filename, F_OK) == 0) {
    FILE *file = fopen(filename, "r");

    while (true) {
      uint8_t buff[4096];
      int bytes = fread(buff, 1, 4096, file);
      if (bytes > 0) {
        uint32_t ignore;
        text_append(b.text, buff, bytes, &ignore, &ignore);
      } else if (bytes == 0) {
        break; // EOF
      } else {
        // TODO: handle error
      }
    }

    fclose(file);
  }

  return b;
}

void write_line(struct text_chunk *chunk, void *userdata) {
  FILE *file = (FILE *)userdata;
  fwrite(chunk->text, 1, chunk->nbytes, file);
  fputc('\n', file);
}

void buffer_to_file(struct buffer *buffer) {
  // TODO: handle errors
  FILE *file = fopen(buffer->filename, "w");

  uint32_t nlines = text_num_lines(buffer->text);
  struct text_chunk lastline = text_get_line(buffer->text, nlines - 1);
  uint32_t nlines_to_write = lastline.nbytes == 0 ? nlines - 1 : nlines;

  text_for_each_line(buffer->text, 0, nlines_to_write, write_line, file);
  minibuffer_echo_timeout(4, "wrote %d lines to %s", nlines_to_write,
                          buffer->filename);
  fclose(file);
}

int buffer_add_text(struct buffer *buffer, uint8_t *text, uint32_t nbytes) {
  uint32_t lines_added, cols_added;
  text_insert_at(buffer->text, buffer->dot_line, buffer->dot_col, text, nbytes,
                 &lines_added, &cols_added);
  movev(buffer, lines_added);

  if (lines_added > 0) {
    // does not make sense to use position from another line
    buffer->dot_col = 0;
  }
  moveh(buffer, cols_added);

  return lines_added;
}

void buffer_newline(struct buffer *buffer) {
  buffer_add_text(buffer, (uint8_t *)"\n", 1);
}

uint32_t buffer_add_update_hook(struct buffer *buffer, update_hook_cb hook,
                                void *userdata) {
  struct update_hook *h =
      &buffer->update_hooks.hooks[buffer->update_hooks.nhooks];
  h->callback = hook;
  h->userdata = userdata;

  ++buffer->update_hooks.nhooks;

  // TODO: cant really have this if someone wants to remove a hook
  return buffer->update_hooks.nhooks - 1;
}

struct cmdbuf {
  struct command_list *cmds;
  uint32_t first_line;
};

void render_line(struct text_chunk *line, void *userdata) {
  struct cmdbuf *cmdbuf = (struct cmdbuf *)userdata;
  command_list_draw_text(cmdbuf->cmds, 0, line->line - cmdbuf->first_line,
                         line->text, line->nbytes);
}

void scroll(struct buffer *buffer, int line_delta, int col_delta) {
  uint32_t nlines = text_num_lines(buffer->text);
  int64_t new_line = (int64_t)buffer->scroll_line + line_delta;
  if (new_line >= 0 && new_line < nlines) {
    buffer->scroll_line = (uint32_t)new_line;
  }

  int64_t new_col = (int64_t)buffer->scroll_col + col_delta;
  if (new_col >= 0 &&
      new_col < text_line_length(buffer->text, buffer->dot_line)) {
    buffer->scroll_col = (uint32_t)new_col;
  }
}

void to_relative(struct buffer *buffer, uint32_t line, uint32_t col,
                 int64_t *rel_line, int64_t *rel_col) {
  *rel_col = (int64_t)col - (int64_t)buffer->scroll_col;
  *rel_line = (int64_t)line - (int64_t)buffer->scroll_line;
}

uint32_t visual_dot_col(struct buffer *buffer, uint32_t dot_col) {
  uint32_t visual_dot_col = dot_col;
  struct text_chunk line = text_get_line(buffer->text, buffer->dot_line);
  for (uint32_t bytei = 0;
       bytei <
       text_col_to_byteindex(buffer->text, buffer->dot_line, buffer->dot_col);
       ++bytei) {
    if (line.text[bytei] == '\t') {
      visual_dot_col += 3;
    }
  }

  return visual_dot_col;
}

void buffer_relative_dot_pos(struct buffer *buffer, uint32_t *relline,
                             uint32_t *relcol) {
  int64_t rel_col, rel_line;
  uint32_t visual_col = visual_dot_col(buffer, buffer->dot_col);
  to_relative(buffer, buffer->dot_line, visual_col, &rel_line, &rel_col);

  *relline = rel_line < 0 ? 0 : (uint32_t)rel_line;
  *relcol = rel_col < 0 ? 0 : (uint32_t)rel_col;
}

struct margin buffer_modeline_hook(struct buffer *buffer,
                                   struct command_list *commands,
                                   uint32_t width, uint32_t height,
                                   uint64_t frame_time, void *userdata) {
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

  uint32_t relcol, relline;
  buffer_relative_dot_pos(buffer, &relline, &relcol);

  snprintf(left, 128, "  %-16s (%d, %d)", buffer->name, relline + 1, relcol);
  snprintf(right, 128, "(%.2f ms) %02d:%02d", frame_time / 1e6, lt->tm_hour,
           lt->tm_min);

  snprintf(buf, width * 4, "%s%*s%s", left,
           (int)(width - (strlen(left) + strlen(right))), "", right);

  struct modeline *modeline = (struct modeline *)userdata;
  if (strcmp(buf, (char *)modeline->buffer) != 0) {
    modeline->buffer = realloc(modeline->buffer, width * 4);
    strcpy((char *)modeline->buffer, buf);

    command_list_set_index_color_bg(commands, 8);
    command_list_draw_text(commands, 0, height - 1, modeline->buffer,
                           strlen((char *)modeline->buffer));
    command_list_reset_color(commands);
  }

  struct margin m = {0};
  m.bottom = 1;
  return m;
}

void buffer_update(struct buffer *buffer, uint32_t width, uint32_t height,
                   struct command_list *commands, uint64_t frame_time) {

  if (width == 0 || height == 0) {
    return;
  }

  struct margin total_margins = {0};
  for (uint32_t hooki = 0; hooki < buffer->update_hooks.nhooks; ++hooki) {
    struct update_hook *h = &buffer->update_hooks.hooks[hooki];
    struct margin margins =
        h->callback(buffer, commands, width, height, frame_time, h->userdata);

    if (margins.left > total_margins.left) {
      total_margins.left = margins.left;
    }
    if (margins.right > total_margins.right) {
      total_margins.right = margins.right;
    }
    if (margins.top > total_margins.top) {
      total_margins.top = margins.top;
    }
    if (margins.bottom > total_margins.bottom) {
      total_margins.bottom = margins.bottom;
    }
  }

  // reserve space for modeline
  uint32_t bufheight = height - (total_margins.top + total_margins.bottom);
  uint32_t bufwidth = width - (total_margins.left + total_margins.right);

  int64_t rel_line, rel_col;
  to_relative(buffer, buffer->dot_line, buffer->dot_col, &rel_line, &rel_col);
  int line_delta = 0, col_delta = 0;
  if (rel_line < 0) {
    line_delta = -(int)bufheight / 2;
  } else if (rel_line >= bufheight) {
    line_delta = bufheight / 2;
  }

  if (rel_col < 0) {
    col_delta = rel_col;
  } else if (rel_col > bufwidth) {
    col_delta = rel_col - bufwidth;
  }

  scroll(buffer, line_delta, col_delta);

  struct cmdbuf cmdbuf = (struct cmdbuf){
      .cmds = commands,
      .first_line = buffer->scroll_line,
  };
  text_for_each_line(buffer->text, buffer->scroll_line, bufheight, render_line,
                     &cmdbuf);

  uint32_t nlines = text_num_lines(buffer->text);
  for (uint32_t linei = nlines - buffer->scroll_line; linei < bufheight;
       ++linei) {
    command_list_draw_text(commands, 0, linei, NULL, 0);
  }
}

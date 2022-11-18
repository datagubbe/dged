#include "buffer.h"
#include "binding.h"
#include "display.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct buffer buffer_create(const char *name) {
  struct buffer b =
      (struct buffer){.filename = NULL,
                      .name = name,
                      .text = text_create(10),
                      .dot_col = 0,
                      .dot_line = 0,
                      .modeline_buf = (uint8_t *)malloc(1024),
                      .keymaps = calloc(10, sizeof(struct keymap)),
                      .nkeymaps = 1,
                      .lines_rendered = -1,
                      .nkeymaps_max = 10};

  b.keymaps[0] = keymap_create("buffer-default", 128);
  struct binding bindings[] = {
      BINDING(Ctrl, 'B', "backward-char"),
      BINDING(Ctrl, 'F', "forward-char"),

      BINDING(Ctrl, 'P', "backward-line"),
      BINDING(Ctrl, 'N', "forward-line"),

      BINDING(Ctrl, 'A', "beginning-of-line"),
      BINDING(Ctrl, 'E', "end-of-line"),

      BINDING(Ctrl, 'M', "newline"),

      BINDING(Ctrl, '?', "backward-delete-char"),
  };
  keymap_bind_keys(&b.keymaps[0], bindings,
                   sizeof(bindings) / sizeof(bindings[0]));

  return b;
}

void buffer_destroy(struct buffer *buffer) {
  free(buffer->modeline_buf);
  text_destroy(buffer->text);
  free(buffer->text);
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

struct buffer buffer_from_file(const char *filename) {
  // TODO: create a reader for the file that calls add_text
  return (struct buffer){.filename = filename, .name = filename};
}

int buffer_to_file(struct buffer *buffer) { return 0; }

int buffer_add_text(struct buffer *buffer, uint8_t *text, uint32_t nbytes) {
  uint32_t lines_added, cols_added;
  text_append(buffer->text, buffer->dot_line, buffer->dot_col, text, nbytes,
              &lines_added, &cols_added);
  movev(buffer, lines_added);
  moveh(buffer, cols_added);

  return lines_added;
}

void buffer_newline(struct buffer *buffer) {
  buffer_add_text(buffer, (uint8_t *)"\n", 1);
}

bool modeline_update(struct buffer *buffer, uint32_t width) {
  char buf[width * 4];

  time_t now = time(NULL);
  struct tm *lt = localtime(&now);
  char left[128], right[128];
  snprintf(left, 128, "--- %-16s (%d, %d)", buffer->name, buffer->dot_line + 1,
           buffer->dot_col);
  snprintf(right, 128, "%02d:%02d", lt->tm_hour, lt->tm_min);

  snprintf(buf, width * 4, "\x1b[100m%s%*s%s\x1b[0m", left,
           (int)(width - (strlen(left) + strlen(right))), "", right);
  if (strcmp(buf, (char *)buffer->modeline_buf) != 0) {
    buffer->modeline_buf = realloc(buffer->modeline_buf, width * 4);
    strcpy((char *)buffer->modeline_buf, buf);
    return true;
  } else {
    return false;
  }
}

struct buffer_update buffer_begin_frame(struct buffer *buffer, uint32_t width,
                                        uint32_t height, alloc_fn frame_alloc) {
  // reserve space for modeline
  uint32_t bufheight = height - 1;
  uint32_t nlines =
      buffer->lines_rendered > bufheight ? bufheight : buffer->lines_rendered;

  struct render_cmd *cmds =
      (struct render_cmd *)frame_alloc(sizeof(struct render_cmd) * (height));

  uint32_t ncmds = text_render(buffer->text, 0, nlines, cmds, nlines);

  buffer->lines_rendered = text_num_lines(buffer->text);

  if (modeline_update(buffer, width)) {
    cmds[ncmds] = (struct render_cmd){
        .col = 0,
        .row = height - 1,
        .data = buffer->modeline_buf,
        .len = strlen((char *)buffer->modeline_buf),
    };
    ++ncmds;
  }

  return (struct buffer_update){.cmds = cmds, .ncmds = ncmds};
}

void buffer_end_frame(struct buffer *buffer, struct buffer_update *upd) {}

#define _DEFAULT_SOURCE
#include "display.h"

#include "buffer.h"

#include <stdarg.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define ESC 0x1b

struct display display_create() {

  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0) {
    // TODO: if it fails to fetch, do something?
    return (struct display){
        .height = 0,
        .width = 0,
    };
  }

  // save old settings
  struct termios orig_term;
  tcgetattr(0, &orig_term);

  // set terminal to raw mode
  struct termios term;
  cfmakeraw(&term);
  // TODO: move to kbd?
  term.c_cc[VMIN] = 0;
  term.c_cc[VTIME] = 0;

  tcsetattr(0, TCSADRAIN, &term);

  return (struct display){
      .orig_term = orig_term,
      .term = term,
      .height = ws.ws_row,
      .width = ws.ws_col,
  };
}

void display_destroy(struct display *display) {
  // reset old terminal mode
  tcsetattr(0, TCSADRAIN, &display->orig_term);
}

void putbytes(uint8_t *line_bytes, uint32_t line_length) {
  for (uint32_t bytei = 0; bytei < line_length; ++bytei) {
    uint8_t byte = line_bytes[bytei];

    if (byte == '\t') {
      fputs("    ", stdout);
    } else {
      fputc(byte, stdout);
    }
  }
}

void putbyte(uint8_t c) { putc(c, stdout); }

void put_ansiparm(int n) {
  int q = n / 10;
  if (q != 0) {
    int r = q / 10;
    if (r != 0) {
      putbyte((r % 10) + '0');
    }
    putbyte((q % 10) + '0');
  }
  putbyte((n % 10) + '0');
}

void display_move_cursor(struct display *display, uint32_t row, uint32_t col) {
  putbyte(ESC);
  putbyte('[');
  put_ansiparm(row + 1);
  putbyte(';');
  put_ansiparm(col + 1);
  putbyte('H');
}

void display_clear(struct display *display) {
  display_move_cursor(display, 0, 0);
  uint8_t bytes[] = {ESC, '[', 'J'};
  putbytes(bytes, 3);
}

void delete_to_eol() {
  uint8_t bytes[] = {ESC, '[', 'K'};
  putbytes(bytes, 3);
}

struct command_list *command_list_create(uint32_t capacity, alloc_fn allocator,
                                         uint32_t xoffset, uint32_t yoffset) {
  struct command_list *command_list = allocator(sizeof(struct command_list));

  command_list->capacity = capacity;
  command_list->ncmds = 0;
  command_list->xoffset = xoffset;
  command_list->yoffset = yoffset;
  command_list->format_len = 0;

  command_list->cmds = allocator(sizeof(struct render_command) * capacity);

  return command_list;
}

void push_format(struct command_list *list, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  if (list->format_len == 0) {
    list->format[0] = ESC;
    list->format[1] = '[';

    list->format_len = 2;
  }

  if (list->format_len > 2) {
    list->format[list->format_len] = ';';
    ++list->format_len;
  }

  uint32_t format_space_left = sizeof(list->format) - 1 - list->format_len;
  list->format_len += vsnprintf((char *)(list->format + list->format_len),
                                format_space_left, fmt, args);

  va_end(args);
}

void flush_format(struct command_list *list, uint32_t col, uint32_t row) {
  list->format[list->format_len] = 'm';
  ++list->format_len;

  struct render_command *cmd = &list->cmds[list->ncmds];
  cmd->data = list->format;
  cmd->col = col + list->xoffset;
  cmd->row = row + list->yoffset;
  cmd->len = list->format_len;

  list->format_len = 0;

  ++list->ncmds;
}

void command_list_draw_text(struct command_list *list, uint32_t col,
                            uint32_t row, uint8_t *data, uint32_t len) {
  uint32_t needed_capacity = list->ncmds + 1;
  if (list->format_len > 0) {
    ++needed_capacity;
  }

  if (needed_capacity > list->capacity) {
    // TODO: better
    return;
  }

  if (list->format_len > 0) {
    flush_format(list, col, row);
  }

  struct render_command *cmd = &list->cmds[list->ncmds];
  cmd->data = data;
  cmd->col = col + list->xoffset;
  cmd->row = row + list->yoffset;
  cmd->len = len;

  ++list->ncmds;
}

void command_list_set_index_color_fg(struct command_list *list,
                                     uint8_t color_idx) {
  if (color_idx < 8) {
    push_format(list, "%d", 30 + color_idx);
  } else if (color_idx < 16) {
    push_format(list, "%d", 90 + color_idx);
  } else {
    push_format(list, "30;5;%d", color_idx);
  }
}
void command_list_set_color_fg(struct command_list *list, uint8_t red,
                               uint8_t green, uint8_t blue) {
  push_format(list, "30;2;%d;%d;%d", red, green, blue);
}

void command_list_set_index_color_bg(struct command_list *list,
                                     uint8_t color_idx) {
  if (color_idx < 8) {
    push_format(list, "%d", 40 + color_idx);
  } else if (color_idx < 16) {
    push_format(list, "%d", 100 + color_idx);
  } else {
    push_format(list, "40;5;%d", color_idx);
  }
}

void command_list_set_color_bg(struct command_list *list, uint8_t red,
                               uint8_t green, uint8_t blue) {
  push_format(list, "40;2;%d;%d;%d", red, green, blue);
}

void command_list_reset_color(struct command_list *list) {
  push_format(list, "0");
}

void display_render(struct display *display,
                    struct command_list *command_list) {

  struct command_list *cl = command_list;

  for (uint64_t cmdi = 0; cmdi < cl->ncmds; ++cmdi) {
    struct render_command *cmd = &cl->cmds[cmdi];
    display_move_cursor(display, cmd->row, cmd->col);
    putbytes(cmd->data, cmd->len);
    delete_to_eol();
  }
}

void display_begin_render(struct display *display) {}
void display_end_render(struct display *display) { fflush(stdout); }

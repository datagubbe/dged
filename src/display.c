#define _DEFAULT_SOURCE
#include "display.h"

#include "buffer.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define ESC 0x1b

enum render_cmd_type {
  RenderCommand_DrawText = 0,
  RenderCommand_PushFormat = 1,
  RenderCommand_Repeat = 2,
  RenderCommand_ClearFormat = 3,
  RenderCommand_SetShowWhitespace = 4,
};

struct render_command {
  enum render_cmd_type type;
  union {
    struct draw_text_cmd *draw_txt;
    struct push_fmt_cmd *push_fmt;
    struct repeat_cmd *repeat;
    struct show_ws_cmd *show_ws;
  };
};

struct draw_text_cmd {
  uint32_t col;
  uint32_t row;

  uint8_t *data;
  uint32_t len;
};

struct push_fmt_cmd {
  uint8_t fmt[64];
  uint32_t len;
};

struct repeat_cmd {
  uint32_t col;
  uint32_t row;
  uint8_t c;
  uint32_t nrepeat;
};

struct show_ws_cmd {
  bool show;
};

struct command_list {
  struct render_command *cmds;
  uint64_t ncmds;
  uint64_t capacity;

  uint32_t xoffset;
  uint32_t yoffset;

  alloc_fn allocator;

  char name[16];
};

struct winsize getsize() {
  struct winsize ws;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
  return ws;
}

struct display display_create() {

  struct winsize ws = getsize();

  // save old settings
  struct termios orig_term;
  tcgetattr(0, &orig_term);

  // set terminal to raw mode
  struct termios term;
  cfmakeraw(&term);

  tcsetattr(0, TCSADRAIN, &term);

  return (struct display){
      .orig_term = orig_term,
      .term = term,
      .height = ws.ws_row,
      .width = ws.ws_col,
  };
}

void display_resize(struct display *display) {
  struct winsize sz = getsize();
  display->width = sz.ws_col;
  display->height = sz.ws_row;
}

void display_destroy(struct display *display) {
  // reset old terminal mode
  tcsetattr(0, TCSADRAIN, &display->orig_term);
}

void putbyte(uint8_t c) {
  if (c != '\r') {
    putc(c, stdout);
  }
}

void putbyte_ws(uint8_t c, bool show_whitespace) {
  if (show_whitespace && c == '\t') {
    fputs("\x1b[90m →  \x1b[0m", stdout);
  } else if (show_whitespace && c == ' ') {
    fputs("\x1b[90m·\x1b[0m", stdout);
  } else {
    putbyte(c);
  }
}

void putbytes(uint8_t *line_bytes, uint32_t line_length, bool show_whitespace) {
  for (uint32_t bytei = 0; bytei < line_length; ++bytei) {
    uint8_t byte = line_bytes[bytei];
    putbyte_ws(byte, show_whitespace);
  }
}

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
  putbytes(bytes, 3, false);
}

struct command_list *command_list_create(uint32_t capacity, alloc_fn allocator,
                                         uint32_t xoffset, uint32_t yoffset,
                                         const char *name) {
  struct command_list *command_list = allocator(sizeof(struct command_list));

  command_list->capacity = capacity;
  command_list->ncmds = 0;
  command_list->xoffset = xoffset;
  command_list->yoffset = yoffset;
  strncpy(command_list->name, name, 15);

  command_list->cmds = allocator(sizeof(struct render_command) * capacity);
  command_list->allocator = allocator;

  return command_list;
}

struct render_command *add_command(struct command_list *list,
                                   enum render_cmd_type tp) {
  if (list->ncmds == list->capacity) {
    // TODO: better
    return NULL;
  }

  struct render_command *cmd = &list->cmds[list->ncmds];
  cmd->type = tp;
  switch (tp) {
  case RenderCommand_DrawText:
    cmd->draw_txt = list->allocator(sizeof(struct draw_text_cmd));
    break;
  case RenderCommand_Repeat:
    cmd->repeat = list->allocator(sizeof(struct repeat_cmd));
    break;
  case RenderCommand_PushFormat:
    cmd->push_fmt = list->allocator(sizeof(struct push_fmt_cmd));
    break;
  case RenderCommand_SetShowWhitespace:
    cmd->show_ws = list->allocator(sizeof(struct show_ws_cmd));
    break;
  case RenderCommand_ClearFormat:
    break;
  }
  ++list->ncmds;
  return cmd;
}

void command_list_draw_text(struct command_list *list, uint32_t col,
                            uint32_t row, uint8_t *data, uint32_t len) {
  struct draw_text_cmd *cmd =
      add_command(list, RenderCommand_DrawText)->draw_txt;
  cmd->data = data;
  cmd->col = col + list->xoffset;
  cmd->row = row + list->yoffset;
  cmd->len = len;
}

void command_list_draw_text_copy(struct command_list *list, uint32_t col,
                                 uint32_t row, uint8_t *data, uint32_t len) {
  uint8_t *bytes = (uint8_t *)list->allocator(len);
  memcpy(bytes, data, len);

  command_list_draw_text(list, col, row, bytes, len);
}

void command_list_draw_repeated(struct command_list *list, uint32_t col,
                                uint32_t row, uint8_t c, uint32_t nrepeat) {
  struct repeat_cmd *cmd = add_command(list, RenderCommand_Repeat)->repeat;
  cmd->col = col;
  cmd->row = row;
  cmd->c = c;
  cmd->nrepeat = nrepeat;
}

void command_list_set_index_color_fg(struct command_list *list,
                                     uint8_t color_idx) {
  struct push_fmt_cmd *cmd =
      add_command(list, RenderCommand_PushFormat)->push_fmt;

  if (color_idx < 8) {
    cmd->len = snprintf((char *)cmd->fmt, 64, "%d", 30 + color_idx);
  } else if (color_idx < 16) {
    cmd->len = snprintf((char *)cmd->fmt, 64, "%d", 90 + color_idx - 8);
  } else {
    cmd->len = snprintf((char *)cmd->fmt, 64, "38;5;%d", color_idx);
  }
}

void command_list_set_color_fg(struct command_list *list, uint8_t red,
                               uint8_t green, uint8_t blue) {
  struct push_fmt_cmd *cmd =
      add_command(list, RenderCommand_PushFormat)->push_fmt;
  cmd->len = snprintf((char *)cmd->fmt, 64, "38;2;%d;%d;%d", red, green, blue);
}

void command_list_set_index_color_bg(struct command_list *list,
                                     uint8_t color_idx) {
  struct push_fmt_cmd *cmd =
      add_command(list, RenderCommand_PushFormat)->push_fmt;
  if (color_idx < 8) {
    cmd->len = snprintf((char *)cmd->fmt, 64, "%d", 40 + color_idx);
  } else if (color_idx < 16) {
    cmd->len = snprintf((char *)cmd->fmt, 64, "%d", 100 + color_idx - 8);
  } else {
    cmd->len = snprintf((char *)cmd->fmt, 64, "48;5;%d", color_idx);
  }
}

void command_list_set_color_bg(struct command_list *list, uint8_t red,
                               uint8_t green, uint8_t blue) {
  struct push_fmt_cmd *cmd =
      add_command(list, RenderCommand_PushFormat)->push_fmt;
  cmd->len = snprintf((char *)cmd->fmt, 64, "48;2;%d;%d;%d", red, green, blue);
}

void command_list_reset_color(struct command_list *list) {
  add_command(list, RenderCommand_ClearFormat);
}

void command_list_set_show_whitespace(struct command_list *list, bool show) {
  add_command(list, RenderCommand_SetShowWhitespace)->show_ws->show = show;
}

void display_render(struct display *display,
                    struct command_list *command_list) {

  struct command_list *cl = command_list;
  uint8_t fmt_stack[256] = {0};
  fmt_stack[0] = ESC;
  fmt_stack[1] = '[';
  fmt_stack[2] = '0';
  uint32_t fmt_stack_len = 3;
  bool show_whitespace_state = false;

  for (uint64_t cmdi = 0; cmdi < cl->ncmds; ++cmdi) {
    struct render_command *cmd = &cl->cmds[cmdi];
    switch (cmd->type) {
    case RenderCommand_DrawText: {
      struct draw_text_cmd *txt_cmd = cmd->draw_txt;
      display_move_cursor(display, txt_cmd->row + cl->yoffset,
                          txt_cmd->col + cl->xoffset);
      putbytes(fmt_stack, fmt_stack_len, false);
      putbyte('m');
      putbytes(txt_cmd->data, txt_cmd->len, show_whitespace_state);
      break;
    }

    case RenderCommand_Repeat: {
      struct repeat_cmd *repeat_cmd = cmd->repeat;
      display_move_cursor(display, repeat_cmd->row + cl->yoffset,
                          repeat_cmd->col + cl->xoffset);
      putbytes(fmt_stack, fmt_stack_len, false);
      putbyte('m');
      if (show_whitespace_state) {
        for (uint32_t i = 0; i < repeat_cmd->nrepeat; ++i) {
          putbyte_ws(repeat_cmd->c, show_whitespace_state);
        }
      } else {
        char *buf = malloc(repeat_cmd->nrepeat + 1);
        memset(buf, repeat_cmd->c, repeat_cmd->nrepeat);
        buf[repeat_cmd->nrepeat] = '\0';
        fputs(buf, stdout);
        free(buf);
      }
      break;
    }

    case RenderCommand_PushFormat: {
      struct push_fmt_cmd *fmt_cmd = cmd->push_fmt;

      fmt_stack[fmt_stack_len] = ';';
      ++fmt_stack_len;

      memcpy(fmt_stack + fmt_stack_len, fmt_cmd->fmt, fmt_cmd->len);
      fmt_stack_len += fmt_cmd->len;
      break;
    }

    case RenderCommand_ClearFormat:
      fmt_stack_len = 3;
      break;

    case RenderCommand_SetShowWhitespace:
      show_whitespace_state = cmd->show_ws->show;
      break;
    }
  }
}

void hide_cursor() {
  putbyte(ESC);
  putbyte('[');
  putbyte('?');
  putbyte('2');
  putbyte('5');
  putbyte('l');
}

void show_cursor() {
  putbyte(ESC);
  putbyte('[');
  putbyte('?');
  putbyte('2');
  putbyte('5');
  putbyte('h');
}

void display_begin_render(struct display *display) { hide_cursor(); }
void display_end_render(struct display *display) {
  show_cursor();
  fflush(stdout);
}

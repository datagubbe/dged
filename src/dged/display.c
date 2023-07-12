#define _DEFAULT_SOURCE
#include "display.h"

#include "buffer.h"
#include "utf8.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define ESC 0x1b

struct display {
  struct termios term;
  struct termios orig_term;
  uint32_t width;
  uint32_t height;
};

enum render_cmd_type {
  RenderCommand_DrawText = 0,
  RenderCommand_PushFormat = 1,
  RenderCommand_Repeat = 2,
  RenderCommand_ClearFormat = 3,
  RenderCommand_SetShowWhitespace = 4,
  RenderCommand_DrawList = 5,
};

struct render_command {
  enum render_cmd_type type;
  union {
    struct draw_text_cmd *draw_txt;
    struct push_fmt_cmd *push_fmt;
    struct repeat_cmd *repeat;
    struct show_ws_cmd *show_ws;
    struct draw_list_cmd *draw_list;
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
  int32_t c;
  uint32_t nrepeat;
};

struct show_ws_cmd {
  bool show;
};

struct draw_list_cmd {
  struct command_list *list;
};

struct command_list {
  struct render_command *cmds;
  uint64_t ncmds;
  uint64_t capacity;

  uint32_t xoffset;
  uint32_t yoffset;

  void *(*allocator)(size_t);

  char name[16];

  struct command_list *next_list;
};

struct winsize getsize() {
  struct winsize ws;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
  return ws;
}

struct display *display_create() {

  struct winsize ws = getsize();

  // save old settings
  struct termios orig_term;
  tcgetattr(0, &orig_term);

  // set terminal to raw mode
  struct termios term = {0};
  cfmakeraw(&term);

  tcsetattr(0, TCSADRAIN, &term);

  struct display *d = calloc(1, sizeof(struct display));
  d->orig_term = orig_term;
  d->term = term;
  d->height = ws.ws_row;
  d->width = ws.ws_col;
  return d;
}

void display_resize(struct display *display) {
  struct winsize sz = getsize();
  display->width = sz.ws_col;
  display->height = sz.ws_row;
}

void display_destroy(struct display *display) {
  // reset old terminal mode
  tcsetattr(0, TCSADRAIN, &display->orig_term);

  free(display);
}

uint32_t display_width(struct display *display) { return display->width; }
uint32_t display_height(struct display *display) { return display->height; }

void putch(uint8_t c) {
  if (c != '\r') {
    putc(c, stdout);
  }
}

void putch_ws(uint8_t c, bool show_whitespace) {
  if (show_whitespace && c == '\t') {
    fputs("\x1b[90m →  \x1b[39m", stdout);
  } else if (show_whitespace && c == ' ') {
    fputs("\x1b[90m·\x1b[39m", stdout);
  } else {
    putch(c);
  }
}

void putbytes(uint8_t *line_bytes, uint32_t line_length, bool show_whitespace) {
  for (uint32_t bytei = 0; bytei < line_length; ++bytei) {
    putch_ws(line_bytes[bytei], show_whitespace);
  }
}

void put_ansiparm(int n) {
  int q = n / 10;
  if (q != 0) {
    int r = q / 10;
    if (r != 0) {
      putch((r % 10) + '0');
    }
    putch((q % 10) + '0');
  }
  putch((n % 10) + '0');
}

void display_move_cursor(struct display *display, uint32_t row, uint32_t col) {
  putch(ESC);
  putch('[');
  put_ansiparm(row + 1);
  putch(';');
  put_ansiparm(col + 1);
  putch('H');
}

void display_clear(struct display *display) {
  display_move_cursor(display, 0, 0);
  uint8_t bytes[] = {ESC, '[', 'J'};
  putbytes(bytes, 3, false);
}

struct command_list *command_list_create(uint32_t initial_capacity,
                                         void *(*allocator)(size_t),
                                         uint32_t xoffset, uint32_t yoffset,
                                         const char *name) {
  struct command_list *command_list = allocator(sizeof(struct command_list));

  command_list->capacity = initial_capacity;
  command_list->ncmds = 0;
  command_list->xoffset = xoffset;
  command_list->yoffset = yoffset;
  command_list->next_list = NULL;
  strncpy(command_list->name, name, 15);

  command_list->cmds =
      allocator(sizeof(struct render_command) * initial_capacity);
  command_list->allocator = allocator;

  return command_list;
}

struct render_command *add_command(struct command_list *list,
                                   enum render_cmd_type tp) {
  struct command_list *l = list;
  struct command_list *n = l->next_list;

  // scan through lists for one with capacity
  while (l->ncmds == l->capacity && n != NULL) {
    l = n;
    n = l->next_list;
  }

  if (l->ncmds == l->capacity && n == NULL) {
    l->next_list = command_list_create(l->capacity, l->allocator, l->xoffset,
                                       l->yoffset, l->name);
    l = l->next_list;
  }

  struct render_command *cmd = &l->cmds[l->ncmds];
  cmd->type = tp;
  switch (tp) {
  case RenderCommand_DrawText:
    cmd->draw_txt = l->allocator(sizeof(struct draw_text_cmd));
    break;
  case RenderCommand_Repeat:
    cmd->repeat = l->allocator(sizeof(struct repeat_cmd));
    break;
  case RenderCommand_PushFormat:
    cmd->push_fmt = l->allocator(sizeof(struct push_fmt_cmd));
    break;
  case RenderCommand_SetShowWhitespace:
    cmd->show_ws = l->allocator(sizeof(struct show_ws_cmd));
    break;
  case RenderCommand_ClearFormat:
    break;
  case RenderCommand_DrawList:
    cmd->draw_list = l->allocator(sizeof(struct draw_list_cmd));
    break;
  default:
    assert(false);
  }

  ++l->ncmds;
  return cmd;
}

void command_list_draw_text(struct command_list *list, uint32_t col,
                            uint32_t row, uint8_t *data, uint32_t len) {
  struct draw_text_cmd *cmd =
      add_command(list, RenderCommand_DrawText)->draw_txt;
  cmd->data = data;
  cmd->col = col;
  cmd->row = row;
  cmd->len = len;
}

void command_list_draw_text_copy(struct command_list *list, uint32_t col,
                                 uint32_t row, uint8_t *data, uint32_t len) {
  uint8_t *bytes = (uint8_t *)list->allocator(len);
  memcpy(bytes, data, len);

  command_list_draw_text(list, col, row, bytes, len);
}

void command_list_draw_repeated(struct command_list *list, uint32_t col,
                                uint32_t row, int32_t c, uint32_t nrepeat) {
  struct repeat_cmd *cmd = add_command(list, RenderCommand_Repeat)->repeat;
  cmd->col = col;
  cmd->row = row;
  cmd->c = c;
  cmd->nrepeat = nrepeat;
}

void command_list_draw_command_list(struct command_list *list,
                                    struct command_list *to_draw) {
  struct draw_list_cmd *cmd =
      add_command(list, RenderCommand_DrawList)->draw_list;
  cmd->list = to_draw;
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

  while (cl != NULL) {

    for (uint64_t cmdi = 0; cmdi < cl->ncmds; ++cmdi) {
      struct render_command *cmd = &cl->cmds[cmdi];
      switch (cmd->type) {
      case RenderCommand_DrawText: {
        struct draw_text_cmd *txt_cmd = cmd->draw_txt;
        display_move_cursor(display, txt_cmd->row + cl->yoffset,
                            txt_cmd->col + cl->xoffset);
        putbytes(fmt_stack, fmt_stack_len, false);
        putch('m');
        putbytes(txt_cmd->data, txt_cmd->len, show_whitespace_state);
        break;
      }

      case RenderCommand_Repeat: {
        struct repeat_cmd *repeat_cmd = cmd->repeat;
        display_move_cursor(display, repeat_cmd->row + cl->yoffset,
                            repeat_cmd->col + cl->xoffset);
        putbytes(fmt_stack, fmt_stack_len, false);
        putch('m');
        uint32_t nbytes = utf8_nbytes((uint8_t *)&repeat_cmd->c, 4, 1);
        for (uint32_t i = 0; i < repeat_cmd->nrepeat; ++i) {
          putbytes((uint8_t *)&repeat_cmd->c, nbytes, show_whitespace_state);
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

      case RenderCommand_DrawList:
        display_render(display, cmd->draw_list->list);
        break;
      }
    }
    cl = cl->next_list;
  }
}

void hide_cursor() {
  putch(ESC);
  putch('[');
  putch('?');
  putch('2');
  putch('5');
  putch('l');
}

void show_cursor() {
  putch(ESC);
  putch('[');
  putch('?');
  putch('2');
  putch('5');
  putch('h');
}

void display_begin_render(struct display *display) { hide_cursor(); }
void display_end_render(struct display *display) {
  show_cursor();
  fflush(stdout);
}

#define _DEFAULT_SOURCE
#include "display.h"

#include "buffer.h"
#include "timers.h"
#include "utf8.h"

#include <assert.h>
#include <ctype.h>
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
  union render_cmd_data {
    struct draw_text_cmd *draw_txt;
    struct push_fmt_cmd *push_fmt;
    struct repeat_cmd *repeat;
    struct show_ws_cmd *show_ws;
    struct draw_list_cmd *draw_list;
  } data;
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
  uint32_t c;
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

struct winsize getsize(void) {
  struct winsize ws;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
  return ws;
}

struct display *display_create(void) {

  struct winsize ws = getsize();

  // save old settings
  struct termios orig_term;
  if (tcgetattr(0, &orig_term) < 0) {
    return NULL;
  }

  // set terminal to raw mode
  struct termios term = orig_term;
  cfmakeraw(&term);

  if (tcsetattr(0, TCSADRAIN, &term) < 0) {
    return NULL;
  }

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

void putch(uint8_t c) { putc(c, stdout); }

static void apply_fmt(uint8_t *fmt_stack, uint32_t fmt_stack_len) {
  if (fmt_stack == NULL || fmt_stack_len == 0) {
    return;
  }

  for (uint32_t i = 0; i < fmt_stack_len; ++i) {
    putc(fmt_stack[i], stdout);
  }
  putc('m', stdout);
}

void putch_ws(uint8_t c, bool show_whitespace, uint8_t *fmt_stack,
              uint32_t fmt_stack_len) {
  // TODO: tab width needs to be sent here
  if (show_whitespace && c == '\t') {
    fputs("\x1b[90m →  \x1b[39m", stdout);
    apply_fmt(fmt_stack, fmt_stack_len);
  } else if (show_whitespace && c == ' ') {
    fputs("\x1b[90m·\x1b[39m", stdout);
    apply_fmt(fmt_stack, fmt_stack_len);
  } else {
    putch(c);
  }
}

void putbytes(uint8_t *line_bytes, uint32_t line_length, bool show_whitespace,
              uint8_t *fmt_stack, uint32_t fmt_stack_len) {
  for (uint32_t bytei = 0; bytei < line_length; ++bytei) {
    putch_ws(line_bytes[bytei], show_whitespace, fmt_stack, fmt_stack_len);
  }
}

void put_ansiparm(int n) {
  int q = n / 10;
  if (q != 0) {
    int r = q / 10;
    if (r != 0) {
      putc((r % 10) + '0', stdout);
    }
    putc((q % 10) + '0', stdout);
  }
  putc((n % 10) + '0', stdout);
}

void display_move_cursor(struct display *display, uint32_t row, uint32_t col) {
  (void)display;

  putc(ESC, stdout);
  putc('[', stdout);
  put_ansiparm(row + 1);
  putc(';', stdout);
  put_ansiparm(col + 1);
  putc('H', stdout);
}

void display_clear(struct display *display) {
  display_move_cursor(display, 0, 0);
  putc(ESC, stdout);
  putc('[', stdout);
  putc('J', stdout);
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
    cmd->data.draw_txt = l->allocator(sizeof(struct draw_text_cmd));
    break;
  case RenderCommand_Repeat:
    cmd->data.repeat = l->allocator(sizeof(struct repeat_cmd));
    break;
  case RenderCommand_PushFormat:
    cmd->data.push_fmt = l->allocator(sizeof(struct push_fmt_cmd));
    break;
  case RenderCommand_SetShowWhitespace:
    cmd->data.show_ws = l->allocator(sizeof(struct show_ws_cmd));
    break;
  case RenderCommand_ClearFormat:
    break;
  case RenderCommand_DrawList:
    cmd->data.draw_list = l->allocator(sizeof(struct draw_list_cmd));
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
      add_command(list, RenderCommand_DrawText)->data.draw_txt;
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
                                uint32_t row, uint32_t c, uint32_t nrepeat) {
  struct repeat_cmd *cmd = add_command(list, RenderCommand_Repeat)->data.repeat;
  cmd->col = col;
  cmd->row = row;
  cmd->c = c;
  cmd->nrepeat = nrepeat;
}

void command_list_draw_command_list(struct command_list *list,
                                    struct command_list *to_draw) {
  struct draw_list_cmd *cmd =
      add_command(list, RenderCommand_DrawList)->data.draw_list;
  cmd->list = to_draw;
}

void command_list_set_index_color_fg(struct command_list *list,
                                     uint8_t color_idx) {
  struct push_fmt_cmd *cmd =
      add_command(list, RenderCommand_PushFormat)->data.push_fmt;

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
      add_command(list, RenderCommand_PushFormat)->data.push_fmt;
  cmd->len = snprintf((char *)cmd->fmt, 64, "38;2;%d;%d;%d", red, green, blue);
}

void command_list_set_index_color_bg(struct command_list *list,
                                     uint8_t color_idx) {
  struct push_fmt_cmd *cmd =
      add_command(list, RenderCommand_PushFormat)->data.push_fmt;
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
      add_command(list, RenderCommand_PushFormat)->data.push_fmt;
  cmd->len = snprintf((char *)cmd->fmt, 64, "48;2;%d;%d;%d", red, green, blue);
}

void command_list_set_inverted_colors(struct command_list *list) {
  struct push_fmt_cmd *cmd =
      add_command(list, RenderCommand_PushFormat)->data.push_fmt;
  cmd->fmt[0] = '7';
  cmd->len = 1;
}

void command_list_reset_color(struct command_list *list) {
  add_command(list, RenderCommand_ClearFormat);
}

void command_list_set_show_whitespace(struct command_list *list, bool show) {
  add_command(list, RenderCommand_SetShowWhitespace)->data.show_ws->show = show;
}

void display_render(struct display *display,
                    struct command_list *command_list) {

  struct command_list *cl = command_list;
  static char name[32] = {0};
  snprintf(name, 31, "display.cl.%s", cl->name);
  struct timer *render_timer = timer_start(name);

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
        struct draw_text_cmd *txt_cmd = cmd->data.draw_txt;
        display_move_cursor(display, txt_cmd->row + cl->yoffset,
                            txt_cmd->col + cl->xoffset);
        apply_fmt(fmt_stack, fmt_stack_len);
        putbytes(txt_cmd->data, txt_cmd->len, show_whitespace_state, fmt_stack,
                 fmt_stack_len);
        break;
      }

      case RenderCommand_Repeat: {
        struct repeat_cmd *repeat_cmd = cmd->data.repeat;
        display_move_cursor(display, repeat_cmd->row + cl->yoffset,
                            repeat_cmd->col + cl->xoffset);
        apply_fmt(fmt_stack, fmt_stack_len);
        struct utf8_codepoint_iterator iter =
            create_utf8_codepoint_iterator((uint8_t *)&repeat_cmd->c, 4, 0);
        struct codepoint *codepoint = utf8_next_codepoint(&iter);
        if (codepoint != NULL) {
          for (uint32_t i = 0; i < repeat_cmd->nrepeat; ++i) {
            putbytes((uint8_t *)&repeat_cmd->c, codepoint->nbytes,
                     show_whitespace_state, fmt_stack, fmt_stack_len);
          }
        }
        break;
      }

      case RenderCommand_PushFormat: {
        struct push_fmt_cmd *fmt_cmd = cmd->data.push_fmt;

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
        show_whitespace_state = cmd->data.show_ws->show;
        break;

      case RenderCommand_DrawList:
        display_render(display, cmd->data.draw_list->list);
        break;
      }
    }
    cl = cl->next_list;
  }

  timer_stop(render_timer);
}

void hide_cursor(void) {
  putc(ESC, stdout);
  putc('[', stdout);
  putc('?', stdout);
  putc('2', stdout);
  putc('5', stdout);
  putc('l', stdout);
}

void show_cursor(void) {
  putc(ESC, stdout);
  putc('[', stdout);
  putc('?', stdout);
  putc('2', stdout);
  putc('5', stdout);
  putc('h', stdout);
}

void display_begin_render(struct display *display) {
  (void)display;
  hide_cursor();
}
void display_end_render(struct display *display) {
  (void)display;

  show_cursor();
  fflush(stdout);
}

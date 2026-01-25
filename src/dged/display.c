#define _DEFAULT_SOURCE
#include "display.h"

#include "buffer.h"
#include "timers.h"
#include "utf8.h"

#include <assert.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
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
  bool render_in_progress;

  uint8_t *outbuf;
  size_t outbuf_size;
  size_t outbuf_current;
};

enum render_cmd_type {
  RenderCommand_DrawText = 0,
  RenderCommand_PushFormat = 1,
  RenderCommand_Repeat = 2,
  RenderCommand_ClearFormat = 3,
  RenderCommand_SetShowWhitespace = 4,
  RenderCommand_DrawList = 5,
  RenderCommand_ClearLine = 6,
};

struct render_command {
  enum render_cmd_type type;
  union render_cmd_data {
    struct draw_text_cmd *draw_txt;
    struct push_fmt_cmd *push_fmt;
    struct repeat_cmd *repeat;
    struct show_ws_cmd *show_ws;
    struct draw_list_cmd *draw_list;
    struct clear_line_cmd *clear_line;
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

struct clear_line_cmd {
  uint32_t col;
  uint32_t row;
};

struct command_list {
  struct render_command *cmds;
  uint64_t ncmds;
  uint64_t capacity;

  uint32_t xoffset;
  uint32_t yoffset;

  uint32_t tab_width;

  void *(*allocator)(size_t);

  char name[16];

  struct command_list *next_list;
};

static void put_ansiparm(struct display *display, uint64_t n);

static void flush_outbuf(struct display *display) {
  fwrite(display->outbuf, 1, display->outbuf_current, stdout);
  fflush(stdout);
  display->outbuf_current = 0;
}

static void putch(struct display *display, uint8_t c) {
  if (display->outbuf_current == display->outbuf_size) {
    flush_outbuf(display);
  }

  display->outbuf[display->outbuf_current] = c;
  ++display->outbuf_current;
}

static void putchars(struct display *display, uint8_t *chars, size_t nchars) {
  if (display->outbuf_current + nchars >= display->outbuf_size) {
    flush_outbuf(display);
  }

  // if bigger than the number of chars,
  // output to stdout directly
  if (nchars > display->outbuf_size) {
    fwrite(chars, 1, nchars, stdout);
    fflush(stdout);
    return;
  }

  memcpy(&display->outbuf[display->outbuf_current], chars, nchars);
  display->outbuf_current += nchars;
}

static void use_alternate_buffer(struct display *display) {
  putch(display, ESC);
  putch(display, '[');
  putch(display, '?');
  put_ansiparm(display, 1049);
  putch(display, 'h');
}

static void use_normal_buffer(struct display *display) {
  putch(display, ESC);
  putch(display, '[');
  putch(display, '?');
  put_ansiparm(display, 1049);
  putch(display, 'l');
}

struct winsize getsize(void) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_row == 0 ||
      ws.ws_col == 0) {
    int fd = open("/dev/tty", O_RDONLY);
    if (fd != -1) {
      ioctl(fd, TIOCGWINSZ, &ws);
      close(fd);
    }
  }

  return ws;
}

struct display *display_create(void) {

  struct display *d = calloc(1, sizeof(struct display));
  d->height = -1;
  d->width = -1;
  d->render_in_progress = false;

  // 32 KiB output buffer
  d->outbuf = calloc(32 * 1024 * 1024, 1);
  d->outbuf_size = 32 * 1024 * 1024;
  d->outbuf_current = 0;

  if (!display_initialize(d)) {
    display_destroy(d);
    return NULL;
  }

  return d;
}

bool display_initialize(struct display *display) {
  struct winsize ws = getsize();
  display->height = ws.ws_row;
  display->width = ws.ws_col;

  // save old settings
  struct termios orig_term = {0};
  if (tcgetattr(0, &orig_term) < 0) {
    return false;
  }

  display->orig_term = orig_term;

  // set terminal to raw mode
  struct termios term = {0};
  cfmakeraw(&term);

  if (tcsetattr(0, TCSADRAIN, &term) < 0) {
    return false;
  }

  display->term = term;

  use_alternate_buffer(display);
  flush_outbuf(display);

  return true;
}

void display_resize(struct display *display) {
  struct winsize sz = getsize();
  display->width = sz.ws_col;
  display->height = sz.ws_row;
}

void display_restore(struct display *display) {
  use_normal_buffer(display);
  flush_outbuf(display);

  // reset old terminal mode
  tcsetattr(0, TCSADRAIN, &display->orig_term);
}

void display_destroy(struct display *display) {

  display_restore(display);

  free(display->outbuf);
  display->outbuf = NULL;
  display->outbuf_current = 0;
  display->outbuf_size = 0;

  free(display);
}

uint32_t display_width(struct display *display) { return display->width; }
uint32_t display_height(struct display *display) { return display->height; }

static void apply_fmt(struct display *display, uint8_t *fmt_stack,
                      uint32_t fmt_stack_len) {
  if (fmt_stack == NULL || fmt_stack_len == 0) {
    return;
  }

  for (uint32_t i = 0; i < fmt_stack_len; ++i) {
    putch(display, fmt_stack[i]);
  }
  putch(display, 'm');
}

static void putch_ws(struct display *display, uint8_t c, bool show_whitespace,
                     uint8_t *fmt_stack, uint32_t fmt_stack_len,
                     uint32_t tab_width) {
  if (show_whitespace && c == '\t') {
    putchars(display, (uint8_t *)"\x1b[90m→", 9);
    uint32_t spaces = tab_width > 0 ? tab_width - 1 : 0;
    for (uint32_t i = 0; i < spaces; ++i) {
      putch(display, ' ');
    }
    putchars(display, (uint8_t *)"\x1b[39m", 6);
    apply_fmt(display, fmt_stack, fmt_stack_len);
  } else if (show_whitespace && c == ' ') {
    putchars(display, (uint8_t *)"\x1b[90m·\x1b[39m", 13);
    apply_fmt(display, fmt_stack, fmt_stack_len);
  } else {
    putch(display, c);
  }
}

static void putbytes(struct display *display, uint8_t *line_bytes,
                     uint32_t line_length, bool show_whitespace,
                     uint8_t *fmt_stack, uint32_t fmt_stack_len,
                     uint32_t tab_width) {
  for (uint32_t bytei = 0; bytei < line_length; ++bytei) {
    putch_ws(display, line_bytes[bytei], show_whitespace, fmt_stack,
             fmt_stack_len, tab_width);
  }
}

static void put_ansiparm(struct display *display, uint64_t n) {
  if (n == 0) {
    putch(display, '0');
    return;
  }

  char chars[20] = {0};
  size_t nchars = 0;
  while (n > 0 && nchars < 20) {
    chars[nchars] = '0' + (n % 10);
    ++nchars;
    n /= 10;
  }

  for (ssize_t i = nchars - 1; i >= 0; --i) {
    putch(display, chars[i]);
  }
}

void display_move_cursor(struct display *display, uint32_t row, uint32_t col) {

  putch(display, ESC);
  putch(display, '[');
  put_ansiparm(display, row + 1);
  putch(display, ';');
  put_ansiparm(display, col + 1);
  putch(display, 'H');
}

void display_clear(struct display *display) {
  display_move_cursor(display, 0, 0);
  putch(display, ESC);
  putch(display, '[');
  putch(display, 'J');
}

static void display_clear_line(struct display *display, uint32_t row,
                               uint32_t col) {
  display_move_cursor(display, row, col);
  putch(display, ESC);
  putch(display, '[');
  putch(display, '0');
  putch(display, 'K');
}

struct command_list *command_list_create(uint32_t initial_capacity,
                                         void *(*allocator)(size_t),
                                         uint32_t xoffset, uint32_t yoffset,
                                         uint32_t tab_width, const char *name) {
  struct command_list *command_list = allocator(sizeof(struct command_list));

  command_list->capacity = initial_capacity;
  command_list->ncmds = 0;
  command_list->xoffset = xoffset;
  command_list->yoffset = yoffset;
  command_list->next_list = NULL;
  command_list->tab_width = tab_width;
  strncpy(command_list->name, name, 15);

  command_list->cmds =
      allocator(sizeof(struct render_command) * initial_capacity);
  command_list->allocator = allocator;

  return command_list;
}

bool command_list_empty(struct command_list *list) { return list->ncmds == 0; }

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
                                       l->yoffset, l->tab_width, l->name);
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
  case RenderCommand_ClearLine:
    cmd->data.clear_line = l->allocator(sizeof(struct clear_line_cmd));
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

void command_list_clear_line(struct command_list *list, uint32_t col,
                             uint32_t row) {
  struct clear_line_cmd *cmd =
      add_command(list, RenderCommand_ClearLine)->data.clear_line;
  cmd->col = col;
  cmd->row = row;
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

void command_list_set_underline(struct command_list *list) {
  struct push_fmt_cmd *cmd =
      add_command(list, RenderCommand_PushFormat)->data.push_fmt;
  cmd->fmt[0] = '4';
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
        apply_fmt(display, fmt_stack, fmt_stack_len);
        putbytes(display, txt_cmd->data, txt_cmd->len, show_whitespace_state,
                 fmt_stack, fmt_stack_len, cl->tab_width);
        break;
      }

      case RenderCommand_Repeat: {
        struct repeat_cmd *repeat_cmd = cmd->data.repeat;
        display_move_cursor(display, repeat_cmd->row + cl->yoffset,
                            repeat_cmd->col + cl->xoffset);
        apply_fmt(display, fmt_stack, fmt_stack_len);
        struct utf8_codepoint_iterator iter =
            create_utf8_codepoint_iterator((uint8_t *)&repeat_cmd->c, 4, 0);
        struct codepoint *codepoint = utf8_next_codepoint(&iter);
        if (codepoint != NULL) {
          for (uint32_t i = 0; i < repeat_cmd->nrepeat; ++i) {
            putbytes(display, (uint8_t *)&repeat_cmd->c, codepoint->nbytes,
                     show_whitespace_state, fmt_stack, fmt_stack_len,
                     cl->tab_width);
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

      case RenderCommand_ClearLine: {
        apply_fmt(display, fmt_stack, fmt_stack_len);
        struct clear_line_cmd *clear_cmd = cmd->data.clear_line;
        display_clear_line(display, cl->yoffset + clear_cmd->row,
                           cl->xoffset + clear_cmd->col);
      } break;

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

void hide_cursor(struct display *display) {
  putch(display, ESC);
  putch(display, '[');
  putch(display, '?');
  putch(display, '2');
  putch(display, '5');
  putch(display, 'l');
}

void show_cursor(struct display *display) {
  putch(display, ESC);
  putch(display, '[');
  putch(display, '?');
  putch(display, '2');
  putch(display, '5');
  putch(display, 'h');
}

static void begin_update(struct display *display) {
  putch(display, ESC);
  putch(display, '[');
  putch(display, '?');
  putch(display, '2');
  putch(display, '0');
  putch(display, '2');
  putch(display, '6');
  putch(display, 'h');
}

static void end_update(struct display *display) {
  putch(display, ESC);
  putch(display, '[');
  putch(display, '?');
  putch(display, '2');
  putch(display, '0');
  putch(display, '2');
  putch(display, '6');
  putch(display, 'l');
}

void display_begin_render(struct display *display) {
  assert(!display->render_in_progress);

  display->render_in_progress = true;
  begin_update(display);
  hide_cursor(display);
}

void display_end_render(struct display *display) {
  display->render_in_progress = false;

  show_cursor(display);
  end_update(display);
  flush_outbuf(display);
}

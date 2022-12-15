#define _DEFAULT_SOURCE
#include "display.h"

#include "buffer.h"

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
  fwrite(line_bytes, 1, line_length, stdout);
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

void display_update(struct display *display, struct render_cmd_buf *cmd_bufs,
                    uint32_t ncmd_bufs, uint32_t currow, uint32_t curcol) {
  for (uint32_t bufi = 0; bufi < ncmd_bufs; ++bufi) {
    struct render_cmd_buf *buf = &cmd_bufs[bufi];
    uint64_t ncmds = buf->ncmds;
    struct render_cmd *cmds = buf->cmds;

    for (uint64_t cmdi = 0; cmdi < ncmds; ++cmdi) {
      struct render_cmd *cmd = &cmds[cmdi];
      display_move_cursor(display, cmd->row + buf->yoffset,
                          cmd->col + buf->xoffset);
      putbytes(cmd->data, cmd->len);
      delete_to_eol();
    }
  }

  display_move_cursor(display, currow, curcol);

  fflush(stdout);
}

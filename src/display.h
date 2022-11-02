#include <stdint.h>

#include <termios.h>

struct display {
  struct termios term;
  struct termios orig_term;
  uint32_t width;
  uint32_t height;
};

struct render_cmd {
  uint32_t col;
  uint32_t row;

  uint8_t *data;
  uint32_t len;
};

struct render_cmd_buf {
  char source[16];
  struct render_cmd *cmds;
  uint64_t ncmds;
};

struct display display_create();
void display_destroy(struct display *display);

void display_clear(struct display *display);
void display_move_cursor(struct display *display, uint32_t row, uint32_t col);
void display_update(struct display *display, struct render_cmd *cmds,
                    uint32_t ncmds, uint32_t currow, uint32_t curcol);

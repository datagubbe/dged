#include <stdint.h>

struct command_list;

struct window {
  uint32_t x;
  uint32_t y;
  uint32_t width;
  uint32_t height;
  struct buffer *buffer;
  struct buffer *prev_buffer;
};

void window_update_buffer(struct window *window, struct command_list *commands,
                          uint64_t frame_time, uint32_t *relline,
                          uint32_t *relcol);

void window_set_buffer(struct window *window, struct buffer *buffer);

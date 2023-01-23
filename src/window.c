#include "buffer.h"
#include "display.h"

void window_update_buffer(struct window *window, struct command_list *commands,
                          uint64_t frame_time, uint32_t *relline,
                          uint32_t *relcol) {
  buffer_update(window->buffer, window->width, window->height, commands,
                frame_time, relline, relcol);
}

#include "minibuffer.h"
#include "buffer.h"
#include "display.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static struct minibuffer {
  struct buffer *buffer;
  struct timespec expires;
} g_minibuffer = {0};

void update(struct buffer *buffer) {
  struct timespec current;
  clock_gettime(CLOCK_MONOTONIC, &current);
  if (current.tv_sec >= g_minibuffer.expires.tv_sec) {
    buffer_clear(buffer);
  }
}

void minibuffer_init(struct buffer *buffer) {
  g_minibuffer.buffer = buffer;
  buffer_add_pre_update_hook(g_minibuffer.buffer, update);
}

void echo(uint32_t timeout, const char *fmt, va_list args) {
  char buff[2048];
  size_t nbytes = vsnprintf(buff, 2048, fmt, args);

  // vsnprintf returns how many characters it would have wanted to write in case
  // of overflow
  buffer_clear(g_minibuffer.buffer);
  buffer_add_text(g_minibuffer.buffer, (uint8_t *)buff,
                  nbytes > 2048 ? 2048 : nbytes);

  clock_gettime(CLOCK_MONOTONIC, &g_minibuffer.expires);
  g_minibuffer.expires.tv_sec += timeout;
}

void minibuffer_echo(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  echo(1000, fmt, args);
  va_end(args);
}

void minibuffer_echo_timeout(uint32_t timeout, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  echo(timeout, fmt, args);
  va_end(args);
}

bool minibuffer_displaying() { return !buffer_is_empty(g_minibuffer.buffer); }
void minibuffer_clear() { g_minibuffer.expires.tv_nsec = 0; }

#include "minibuffer.h"
#include "display.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static struct minibuffer g_minibuffer = {0};

void minibuffer_init(uint32_t row) {
  g_minibuffer.buffer = malloc(4096);
  g_minibuffer.capacity = 4096;
  g_minibuffer.nbytes = 0;
  g_minibuffer.row = row;
  g_minibuffer.dirty = false;
}

void minibuffer_destroy() {
  free(g_minibuffer.buffer);
  g_minibuffer.capacity = 0;
  g_minibuffer.dirty = false;
}

struct minibuffer_update minibuffer_update(alloc_fn frame_alloc) {
  // TODO: multiline
  if (g_minibuffer.nbytes == 0 && !g_minibuffer.dirty) {
    return (struct minibuffer_update){.cmds = NULL, .ncmds = 0};
  }

  struct timespec current;
  clock_gettime(CLOCK_MONOTONIC, &current);
  if (current.tv_sec < g_minibuffer.expires.tv_sec) {
    struct render_cmd *cmds =
        (struct render_cmd *)frame_alloc(sizeof(struct render_cmd));

    cmds[0].col = 0;
    cmds[0].row = g_minibuffer.row;
    cmds[0].data = g_minibuffer.buffer;
    cmds[0].len = g_minibuffer.nbytes;

    g_minibuffer.dirty = false;

    return (struct minibuffer_update){
        .cmds = cmds,
        .ncmds = 1,
    };
  } else {
    g_minibuffer.nbytes = 0;
    g_minibuffer.dirty = false;
    // send a clear draw command
    struct render_cmd *cmds =
        (struct render_cmd *)frame_alloc(sizeof(struct render_cmd));

    cmds[0].col = 0;
    cmds[0].row = g_minibuffer.row;
    cmds[0].data = NULL;
    cmds[0].len = 0;

    return (struct minibuffer_update){
        .cmds = cmds,
        .ncmds = 1,
    };
  }
}

void echo(uint32_t timeout, const char *fmt, va_list args) {
  size_t nbytes =
      vsnprintf((char *)g_minibuffer.buffer, g_minibuffer.capacity, fmt, args);

  // vsnprintf returns how many characters it would have wanted to write in case
  // of overflow
  g_minibuffer.nbytes =
      nbytes > g_minibuffer.capacity ? g_minibuffer.capacity : nbytes;
  g_minibuffer.dirty = true;

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

bool minibuffer_displaying() { return g_minibuffer.nbytes > 0; }
void minibuffer_clear() { g_minibuffer.expires.tv_nsec = 0; }

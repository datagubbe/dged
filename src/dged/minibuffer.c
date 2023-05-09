#include "minibuffer.h"
#include "binding.h"
#include "buffer.h"
#include "command.h"
#include "display.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct minibuffer {
  struct buffer *buffer;
  struct timespec expires;

  char prompt[128];
  struct command_ctx prompt_command_ctx;
  bool prompt_active;
  bool clear;

  void (*update_callback)();

} g_minibuffer = {0};

void draw_prompt(struct command_list *commands, void *userdata) {
  uint32_t len = strlen(g_minibuffer.prompt);
  command_list_set_index_color_fg(commands, 4);
  command_list_draw_text(commands, 0, 0, (uint8_t *)g_minibuffer.prompt, len);
  command_list_reset_color(commands);
}

int32_t minibuffer_execute() {
  if (g_minibuffer.prompt_active) {
    struct command_ctx *c = &g_minibuffer.prompt_command_ctx;

    struct text_chunk line = minibuffer_content();
    char *l = (char *)malloc(line.nbytes + 1);
    memcpy(l, line.text, line.nbytes);
    l[line.nbytes] = '\0';

    // propagate any saved arguments
    char *argv[64];
    for (uint32_t i = 0; i < c->saved_argc; ++i) {
      argv[i] = (char *)c->saved_argv[i];
    }
    argv[c->saved_argc] = l;
    uint32_t argc = c->saved_argc + (line.nbytes > 0 ? 1 : 0);

    // split on ' '
    for (uint32_t bytei = 0; bytei < line.nbytes; ++bytei) {
      uint8_t byte = line.text[bytei];
      if (byte == ' ' && argc < 64) {
        l[bytei] = '\0';
        argv[argc] = l + bytei + 1;
        ++argc;
      }
    }

    minibuffer_abort_prompt();
    int32_t res = execute_command(c->self, c->commands, c->active_window,
                                  c->buffers, argc, (const char **)argv);

    free(l);

    return res;
  } else {
    return 0;
  }
}

struct update_hook_result update(struct buffer_view *view,
                                 struct command_list *commands, uint32_t width,
                                 uint32_t height, uint64_t frame_time,
                                 void *userdata) {
  struct timespec current;
  struct minibuffer *mb = (struct minibuffer *)userdata;
  clock_gettime(CLOCK_MONOTONIC, &current);
  if ((!mb->prompt_active && current.tv_sec >= mb->expires.tv_sec) ||
      mb->clear) {
    buffer_clear(view);
    mb->clear = false;
  }

  struct update_hook_result res = {0};
  if (mb->prompt_active) {
    res.margins.left = strlen(mb->prompt);
    draw_prompt(commands, NULL);
  }

  if (mb->update_callback != NULL) {
    mb->update_callback();
  }

  return res;
}

void minibuffer_init(struct buffer *buffer) {
  if (g_minibuffer.buffer != NULL) {
    return;
  }

  g_minibuffer.buffer = buffer;
  g_minibuffer.clear = false;
  buffer_add_update_hook(g_minibuffer.buffer, update, &g_minibuffer);
}

void echo(uint32_t timeout, const char *fmt, va_list args) {
  if (g_minibuffer.prompt_active || g_minibuffer.buffer == NULL) {
    return;
  }

  char buff[2048];
  size_t nbytes = vsnprintf(buff, 2048, fmt, args);

  // vsnprintf returns how many characters it would have wanted to write in case
  // of overflow
  buffer_set_text(g_minibuffer.buffer, (uint8_t *)buff,
                  nbytes > 2048 ? 2048 : nbytes);
  g_minibuffer.clear = false;

  clock_gettime(CLOCK_MONOTONIC, &g_minibuffer.expires);
  g_minibuffer.expires.tv_sec += timeout;
}

void minibuffer_destroy() {
  command_ctx_free(&g_minibuffer.prompt_command_ctx);
}

struct text_chunk minibuffer_content() {
  return buffer_get_line(g_minibuffer.buffer, 0);
}

struct buffer *minibuffer_buffer() {
  return g_minibuffer.buffer;
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

void minibuffer_set_prompt_internal(const char *fmt, va_list args) {
  vsnprintf(g_minibuffer.prompt, sizeof(g_minibuffer.prompt), fmt, args);
}

int32_t minibuffer_prompt_internal(struct command_ctx command_ctx,
                                   void (*update_callback)(), const char *fmt,
                                   va_list args) {
  if (g_minibuffer.buffer == NULL) {
    return 1;
  }

  minibuffer_clear();
  g_minibuffer.prompt_active = true;

  command_ctx_free(&g_minibuffer.prompt_command_ctx);
  g_minibuffer.prompt_command_ctx = command_ctx;

  minibuffer_set_prompt_internal(fmt, args);

  if (update_callback != NULL) {
    g_minibuffer.update_callback = update_callback;
  }

  return 0;
}

int32_t minibuffer_prompt(struct command_ctx command_ctx, const char *fmt,
                          ...) {
  va_list args;
  va_start(args, fmt);
  int32_t r = minibuffer_prompt_internal(command_ctx, NULL, fmt, args);
  va_end(args);

  return r;
}

int32_t minibuffer_prompt_interactive(struct command_ctx command_ctx,
                                      void (*update_callback)(),
                                      const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int32_t r =
      minibuffer_prompt_internal(command_ctx, update_callback, fmt, args);
  va_end(args);

  return r;
}

void minibuffer_set_prompt(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  minibuffer_set_prompt_internal(fmt, args);
  va_end(args);
}

void minibuffer_abort_prompt() {
  minibuffer_clear();
  g_minibuffer.update_callback = NULL;
  g_minibuffer.prompt_active = false;
}

bool minibuffer_empty() { return !minibuffer_displaying(); }

bool minibuffer_displaying() {
  return g_minibuffer.buffer != NULL && !buffer_is_empty(g_minibuffer.buffer);
}

void minibuffer_clear() {
  g_minibuffer.expires.tv_sec = 0;
  g_minibuffer.clear = true;
}

bool minibuffer_focused() { return g_minibuffer.prompt_active; }
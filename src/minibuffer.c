#include "minibuffer.h"
#include "binding.h"
#include "buffer.h"
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
  struct keymap keymap;
} g_minibuffer = {0};

void draw_prompt(struct text_chunk *line_data, uint32_t line,
                 struct command_list *commands, void *userdata) {
  uint32_t len = strlen(g_minibuffer.prompt);
  command_list_set_index_color_fg(commands, 4);
  command_list_draw_text(commands, 0, line, (uint8_t *)g_minibuffer.prompt,
                         len);
  command_list_reset_color(commands);
}

int32_t execute(struct command_ctx ctx, int argc, const char *argv[]) {
  if (g_minibuffer.prompt_active) {
    struct text_chunk line = buffer_get_line(g_minibuffer.buffer, 0);
    char *l = (char *)malloc(line.nbytes + 1);
    memcpy(l, line.text, line.nbytes);

    // split on ' '
    const char *argv[128] = {l};
    argc = 1;
    for (uint32_t bytei = 0; bytei < line.nbytes; ++bytei) {
      uint8_t byte = line.text[bytei];
      if (byte == ' ') {
        l[bytei] = '\0';
        argv[argc] = l + bytei;
        ++argc;
      }
    }

    minibuffer_abort_prompt();
    struct command_ctx *ctx = &g_minibuffer.prompt_command_ctx;
    uint32_t res = execute_command(ctx->self, ctx->commands, ctx->active_window,
                                   ctx->buffers, argc, argv);

    free(l);
    return res;
  } else {
    return 0;
  }
}

struct command execute_minibuffer_command = {
    .fn = execute,
    .name = "minibuffer-execute",
    .userdata = NULL,
};

int32_t complete(struct command_ctx ctx, int argc, const char *argv[]) {
  return 0;
}

struct command complete_minibuffer_command = {
    .fn = complete,
    .name = "minibuffer-complete",
    .userdata = NULL,
};

struct update_hook_result update(struct buffer *buffer,
                                 struct command_list *commands, uint32_t width,
                                 uint32_t height, uint64_t frame_time,
                                 void *userdata) {
  struct timespec current;
  struct minibuffer *mb = (struct minibuffer *)userdata;
  clock_gettime(CLOCK_MONOTONIC, &current);
  if (!mb->prompt_active && current.tv_sec >= mb->expires.tv_sec) {
    buffer_clear(buffer);
  }

  struct update_hook_result res = {0};
  if (g_minibuffer.prompt_active) {
    res.margins.left = strlen(g_minibuffer.prompt);
    res.line_render_hook.callback = draw_prompt;
  }

  return res;
}

void minibuffer_init(struct buffer *buffer) {
  g_minibuffer.buffer = buffer;
  struct binding bindings[] = {
      ANONYMOUS_BINDING(Ctrl, 'M', &execute_minibuffer_command),
      ANONYMOUS_BINDING(Ctrl, 'I', &complete_minibuffer_command),
  };
  keymap_bind_keys(&g_minibuffer.keymap, bindings,
                   sizeof(bindings) / sizeof(bindings[0]));
  buffer_add_keymap(g_minibuffer.buffer, &g_minibuffer.keymap);
  buffer_add_update_hook(g_minibuffer.buffer, update, &g_minibuffer);
}

void echo(uint32_t timeout, const char *fmt, va_list args) {
  if (g_minibuffer.prompt_active) {
    return;
  }

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

void minibuffer_prompt(struct command_ctx command_ctx, const char *fmt, ...) {
  minibuffer_clear();
  g_minibuffer.prompt_active = true;
  g_minibuffer.prompt_command_ctx = command_ctx;
  va_list args;
  va_start(args, fmt);
  vsnprintf(g_minibuffer.prompt, sizeof(g_minibuffer.prompt), fmt, args);
  va_end(args);
}

void minibuffer_abort_prompt() {
  minibuffer_clear();
  g_minibuffer.prompt_active = false;
}

bool minibuffer_displaying() { return !buffer_is_empty(g_minibuffer.buffer); }
void minibuffer_clear() { buffer_clear(g_minibuffer.buffer); }
bool minibuffer_focused() { return g_minibuffer.prompt_active; }

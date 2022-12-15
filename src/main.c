#include <locale.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "binding.h"
#include "buffer.h"
#include "display.h"
#include "minibuffer.h"
#include "reactor.h"

struct frame_allocator {
  uint8_t *buf;
  size_t offset;
  size_t capacity;
};

struct frame_allocator frame_allocator_create(size_t capacity) {
  return (struct frame_allocator){
      .capacity = capacity, .offset = 0, .buf = (uint8_t *)malloc(capacity)};
}

void *frame_allocator_alloc(struct frame_allocator *alloc, size_t sz) {
  if (alloc->offset + sz > alloc->capacity) {
    return NULL;
  }

  void *mem = alloc->buf + alloc->offset;
  alloc->offset += sz;

  return mem;
}

void frame_allocator_clear(struct frame_allocator *alloc) { alloc->offset = 0; }

struct frame_allocator frame_allocator;

void *frame_alloc(size_t sz) {
  return frame_allocator_alloc(&frame_allocator, sz);
}

bool running = true;

void terminate() { running = false; }

void _abort(struct command_ctx ctx, int argc, const char *argv[]) {
  minibuffer_echo_timeout(4, "💣 aborted");
}

void unimplemented_command(struct command_ctx ctx, int argc,
                           const char *argv[]) {
  minibuffer_echo("TODO: %s is not implemented", (const char *)ctx.userdata);
}
void exit_editor(struct command_ctx ctx, int argc, const char *argv[]) {
  terminate();
}

static struct command GLOBAL_COMMANDS[] = {{
                                               .name = "find-file",
                                               .fn = unimplemented_command,
                                               .userdata = (void *)"find-file",
                                           },
                                           {.name = "abort", .fn = _abort},
                                           {.name = "exit", .fn = exit_editor}};

uint64_t calc_frame_time_ns(struct timespec *timers, uint32_t num_timer_pairs) {
  uint64_t total = 0;
  for (uint32_t ti = 0; ti < num_timer_pairs * 2; ti += 2) {
    struct timespec *start_timer = &timers[ti];
    struct timespec *end_timer = &timers[ti + 1];

    total +=
        ((uint64_t)end_timer->tv_sec * 1e9 + (uint64_t)end_timer->tv_nsec) -
        ((uint64_t)start_timer->tv_sec * 1e9 + (uint64_t)start_timer->tv_nsec);
  }

  return total;
}

struct buffers {
  // TODO: more buffers
  struct buffer buffers[32];
  uint32_t nbuffers;
};

struct window {
  uint32_t x;
  uint32_t y;
  uint32_t width;
  uint32_t height;
  struct buffer *buffer;
};

struct buffer_update window_update_buffer(struct window *window,
                                          uint64_t frame_time) {
  return buffer_update(window->buffer, window->width, window->height,
                       frame_alloc, frame_time);
}

void buffers_init(struct buffers *buffers) { buffers->nbuffers = 0; }

void buffers_add(struct buffers *buffers, struct buffer buffer) {
  buffers->buffers[buffers->nbuffers] = buffer;
  ++buffers->nbuffers;
}

void buffers_destroy(struct buffers *buffers) {
  for (uint32_t bufi = 0; bufi < buffers->nbuffers; ++bufi) {
    buffer_destroy(&buffers->buffers[bufi]);
  }

  buffers->nbuffers = 0;
}

int main(int argc, char *argv[]) {
  const char *filename = NULL;
  if (argc >= 1) {
    filename = argv[1];
  }

  setlocale(LC_ALL, "");

  signal(SIGTERM, terminate);

  frame_allocator = frame_allocator_create(1024 * 1024);

  // create reactor
  struct reactor reactor = reactor_create();

  // initialize display
  struct display display = display_create();
  display_clear(&display);

  // init keyboard
  struct keyboard kbd = keyboard_create(&reactor);

  // commands
  struct commands commands = command_list_create(32);
  register_commands(&commands, GLOBAL_COMMANDS,
                    sizeof(GLOBAL_COMMANDS) / sizeof(GLOBAL_COMMANDS[0]));
  register_commands(&commands, BUFFER_COMMANDS,
                    sizeof(BUFFER_COMMANDS) / sizeof(BUFFER_COMMANDS[0]));

  // keymaps
  struct keymap *current_keymap = NULL;
  struct keymap global_keymap = keymap_create("global", 32);
  struct keymap ctrlx_map = keymap_create("c-x", 32);
  struct binding global_binds[] = {
      PREFIX(Ctrl, 'X', &ctrlx_map),
      BINDING(Ctrl, 'G', "abort"),
  };
  struct binding ctrlx_bindings[] = {
      BINDING(Ctrl, 'C', "exit"),
      BINDING(Ctrl, 'S', "buffer-write-to-file"),
      BINDING(Ctrl, 'F', "find-file"),
  };
  keymap_bind_keys(&global_keymap, global_binds,
                   sizeof(global_binds) / sizeof(global_binds[0]));
  keymap_bind_keys(&ctrlx_map, ctrlx_bindings,
                   sizeof(ctrlx_bindings) / sizeof(ctrlx_bindings[0]));

  struct buffers buflist = {0};
  buffers_init(&buflist);
  struct buffer curbuf = buffer_create("welcome", true);
  if (filename != NULL) {
    curbuf = buffer_from_file(filename, &reactor);
  } else {
    const char *welcome_txt = "Welcome to the editor for datagubbar 👴\n";
    buffer_add_text(&curbuf, (uint8_t *)welcome_txt, strlen(welcome_txt));
  }

  buffers_add(&buflist, curbuf);

  // one main window
  struct window main_window = (struct window){
      .buffer = &curbuf,
      .height = display.height - 1,
      .width = display.width,
      .x = 0,
      .y = 0,
  };

  // and one for the minibuffer
  struct buffer minibuffer = buffer_create("minibuffer", false);
  buffers_add(&buflist, minibuffer);

  minibuffer_init(&minibuffer);
  struct window minibuffer_window = (struct window){
      .buffer = &minibuffer,
      .x = 0,
      .y = display.height - 1,
      .height = 1,
      .width = display.width,
  };

  struct timespec buffer_begin, buffer_end, display_begin, display_end,
      keyboard_begin, keyboard_end;

  uint64_t frame_time = 0;

  struct render_cmd_buf render_bufs[2] = {0};

  struct window *windows[2] = {
      &minibuffer_window,
      &main_window,
  };

  // TODO: not always
  struct window *active_window = &main_window;

  while (running) {

    clock_gettime(CLOCK_MONOTONIC, &buffer_begin);

    // update windows
    uint32_t dot_line = 0, dot_col = 0;
    for (uint32_t windowi = 0; windowi < 2; ++windowi) {
      struct window *win = windows[windowi];
      struct buffer_update buf_upd = window_update_buffer(win, frame_time);
      render_bufs[windowi].cmds = buf_upd.cmds;
      render_bufs[windowi].ncmds = buf_upd.ncmds;
      render_bufs[windowi].xoffset = win->x;
      render_bufs[windowi].yoffset = win->y;
    }

    clock_gettime(CLOCK_MONOTONIC, &buffer_end);

    // update screen
    clock_gettime(CLOCK_MONOTONIC, &display_begin);
    uint32_t relline, relcol;
    buffer_relative_dot_pos(active_window->buffer, &relline, &relcol);
    if (render_bufs[0].ncmds > 0 || render_bufs[1].ncmds > 0) {
      display_update(&display, render_bufs, 2, relline, relcol);
    }

    clock_gettime(CLOCK_MONOTONIC, &display_end);

    reactor_update(&reactor);

    clock_gettime(CLOCK_MONOTONIC, &keyboard_begin);
    struct keymap *local_keymaps = NULL;
    uint32_t nbuffer_keymaps = buffer_keymaps(&curbuf, &local_keymaps);
    struct keyboard_update kbd_upd = keyboard_update(&kbd, &reactor);

    // buffer up chars to handle utf-8 "correctly"
    uint8_t chars[kbd_upd.nkeys];
    uint8_t nchars;
    for (uint32_t ki = 0; ki < kbd_upd.nkeys; ++ki) {
      struct key *k = &kbd_upd.keys[ki];

      struct lookup_result res = {.found = false};
      if (current_keymap != NULL) {
        res = lookup_key(current_keymap, 1, k, &commands);
      } else {
        // check first the global keymap, then the buffer ones
        res = lookup_key(&global_keymap, 1, k, &commands);
        if (!res.found) {
          res = lookup_key(local_keymaps, nbuffer_keymaps, k, &commands);
        }
      }

      if (res.found) {
        if (nchars > 0) {
          buffer_add_text(active_window->buffer, chars, nchars);
          nchars = 0;
        }
        switch (res.type) {
        case BindingType_Command: {
          execute_command(res.command, active_window->buffer, 0, NULL);
          current_keymap = NULL;
          break;
        }
        case BindingType_Keymap: {
          char keyname[16];
          key_name(k, keyname, 16);
          minibuffer_echo("%s", keyname);
          current_keymap = res.keymap;
          break;
        }
        }
      } else if (current_keymap != NULL) {
        if (nchars > 0) {
          buffer_add_text(active_window->buffer, chars, nchars);
          nchars = 0;
        }
        minibuffer_echo_timeout(4, "key is not bound!");
        current_keymap = NULL;
      } else {
        chars[nchars] = k->c;
        ++nchars;
      }
    }
    if (nchars > 0) {
      buffer_add_text(active_window->buffer, chars, nchars);
      nchars = 0;
    }
    clock_gettime(CLOCK_MONOTONIC, &keyboard_end);

    // calculate frame time
    struct timespec timers[] = {buffer_begin, buffer_end,     display_begin,
                                display_end,  keyboard_begin, keyboard_end};
    frame_time = calc_frame_time_ns(timers, 3);

    frame_allocator_clear(&frame_allocator);
  }

  buffers_destroy(&buflist);
  display_clear(&display);
  display_destroy(&display);
  keymap_destroy(&global_keymap);
  command_list_destroy(&commands);

  return 0;
}

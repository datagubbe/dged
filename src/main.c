#include <assert.h>
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
                           const char *argv[]) {}
void exit_editor(struct command_ctx ctx, int argc, const char *argv[]) {
  terminate();
}

static struct command GLOBAL_COMMANDS[] = {
    {.name = "find-file", .fn = unimplemented_command},
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
  };
  struct binding ctrlx_bindings[] = {
      BINDING(Ctrl, 'C', "exit"),
      BINDING(Ctrl, 'G', "abort"),
      BINDING(Ctrl, 'S', "buffer-write-to-file"),
  };
  keymap_bind_keys(&global_keymap, global_binds,
                   sizeof(global_binds) / sizeof(global_binds[0]));
  keymap_bind_keys(&ctrlx_map, ctrlx_bindings,
                   sizeof(ctrlx_bindings) / sizeof(ctrlx_bindings[0]));

  struct buffer curbuf = buffer_create("welcome");
  if (filename != NULL) {
    curbuf = buffer_from_file(filename, &reactor);
  } else {
    const char *welcome_txt = "Welcome to the editor for datagubbar 👴\n";
    buffer_add_text(&curbuf, (uint8_t *)welcome_txt, strlen(welcome_txt));
  }

  minibuffer_init(display.height - 1);

  struct timespec buffer_begin, buffer_end, display_begin, display_end,
      keyboard_begin, keyboard_end;

  uint64_t frame_time = 0;

  struct render_cmd_buf render_bufs[2] = {
      {.source = "minibuffer"},
      {.source = "buffer"},
  };

  while (running) {

    clock_gettime(CLOCK_MONOTONIC, &buffer_begin);

    // update minibuffer
    struct minibuffer_update minibuf_upd = minibuffer_update(frame_alloc);
    render_bufs[0].cmds = minibuf_upd.cmds;
    render_bufs[0].ncmds = minibuf_upd.ncmds;

    // update current buffer
    struct buffer_update buf_upd =
        buffer_update(&curbuf, display.width, display.height - 1, frame_alloc,
                      &reactor, frame_time);
    render_bufs[1].cmds = buf_upd.cmds;
    render_bufs[1].ncmds = buf_upd.ncmds;

    clock_gettime(CLOCK_MONOTONIC, &buffer_end);

    // update screen
    clock_gettime(CLOCK_MONOTONIC, &display_begin);
    if (render_bufs[0].ncmds > 0 || render_bufs[1].ncmds > 0) {
      display_update(&display, render_bufs, 2, buf_upd.dot_line,
                     buf_upd.dot_col);
    }

    clock_gettime(CLOCK_MONOTONIC, &display_end);

    reactor_update(&reactor);

    clock_gettime(CLOCK_MONOTONIC, &keyboard_begin);
    struct keymap *local_keymaps = NULL;
    uint32_t nbuffer_keymaps = buffer_keymaps(&curbuf, &local_keymaps);
    struct keyboard_update kbd_upd = keyboard_update(&kbd, &reactor);
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
        switch (res.type) {
        case BindingType_Command: {
          const char *argv[] = {};
          res.command->fn((struct command_ctx){.current_buffer = &curbuf}, 0,
                          argv);
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
        minibuffer_echo_timeout(4, "key is not bound!");
        current_keymap = NULL;
      } else {
        buffer_add_text(&curbuf, &k->c, 1);
      }
    }
    clock_gettime(CLOCK_MONOTONIC, &keyboard_end);

    // calculate frame time
    struct timespec timers[] = {buffer_begin, buffer_end,     display_begin,
                                display_end,  keyboard_begin, keyboard_end};
    frame_time = calc_frame_time_ns(timers, 3);

    frame_allocator_clear(&frame_allocator);
  }

  display_clear(&display);
  display_destroy(&display);
  keymap_destroy(&global_keymap);
  command_list_destroy(&commands);

  return 0;
}

#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <assert.h>
#include <signal.h>
#include <sys/epoll.h>

#include "binding.h"
#include "buffer.h"
#include "display.h"

struct reactor {
  int epoll_fd;
};

struct reactor reactor_create();
int reactor_register_interest(struct reactor *reactor, int fd);

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

void unimplemented_command(struct buffer *buffer) {}
void exit_editor(struct buffer *buffer) { terminate(); }

static struct command GLOBAL_COMMANDS[] = {
    {.name = "find-file", .fn = unimplemented_command},
    {.name = "exit", .fn = exit_editor}};

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
  struct keyboard kbd = keyboard_create();

  // commands
  struct commands commands = command_list_create(32);
  register_commands(&commands, GLOBAL_COMMANDS,
                    sizeof(GLOBAL_COMMANDS) / sizeof(GLOBAL_COMMANDS[0]));
  register_commands(&commands, BUFFER_COMMANDS,
                    sizeof(BUFFER_COMMANDS) / sizeof(BUFFER_COMMANDS[0]));

  // keymaps
  struct keymap global_keymap = keymap_create("global", 32);
  struct binding global_binds[] = {
      BINDING(Ctrl, 'X', "exit"),
  };
  keymap_bind_keys(&global_keymap, global_binds,
                   sizeof(global_binds) / sizeof(global_binds[0]));

  // TODO: load initial buffer
  struct buffer curbuf = buffer_create("welcome");
  const char *welcome_txt = "Welcome to the editor for datagubbar 👴\n";
  buffer_add_text(&curbuf, (uint8_t *)welcome_txt, strlen(welcome_txt));

  while (running) {
    // update keyboard
    struct keymap *local_keymaps = NULL;
    uint32_t nbuffer_keymaps = buffer_keymaps(&curbuf, &local_keymaps);
    struct keyboard_update kbd_upd = keyboard_begin_frame(&kbd);
    for (uint32_t ki = 0; ki < kbd_upd.nkeys; ++ki) {
      struct key *k = &kbd_upd.keys[ki];

      // check first the global keymap, then the buffer ones
      struct command *cmd = lookup_key(&global_keymap, 1, k, &commands);
      if (cmd == NULL) {
        cmd = lookup_key(local_keymaps, nbuffer_keymaps, k, &commands);
      }

      if (cmd != NULL) {
        cmd->fn(&curbuf);
      } else {
        buffer_add_text(&curbuf, &k->c, 1);
      }
    }

    // update current buffer
    struct buffer_update buf_upd = buffer_begin_frame(
        &curbuf, display.width, display.height - 1, frame_alloc);

    // update screen
    if (buf_upd.ncmds > 0) {
      display_update(&display, buf_upd.cmds, buf_upd.ncmds, curbuf.dot_line,
                     curbuf.dot_col);
    }

    buffer_end_frame(&curbuf, &buf_upd);
    frame_allocator_clear(&frame_allocator);
  }

  display_clear(&display);
  display_destroy(&display);
  keymap_destroy(&global_keymap);
  command_list_destroy(&commands);

  return 0;
}

struct reactor reactor_create() {
  int epollfd = epoll_create1(0);
  if (epollfd == -1) {
    perror("epoll_create1");
  }

  return (struct reactor){.epoll_fd = epollfd};
}

int reactor_register_interest(struct reactor *reactor, int fd) {
  struct epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.fd = fd;
  if (epoll_ctl(reactor->epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
    perror("epoll_ctl");
    return -1;
  }

  return fd;
}

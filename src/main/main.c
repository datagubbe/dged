#include <getopt.h>
#include <locale.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "dged/allocator.h"
#include "dged/binding.h"
#include "dged/buffer.h"
#include "dged/buffers.h"
#include "dged/display.h"
#include "dged/lang.h"
#include "dged/minibuffer.h"
#include "dged/path.h"
#include "dged/reactor.h"
#include "dged/settings.h"

#include "bindings.h"
#include "cmds.h"

static struct frame_allocator frame_allocator;

void *frame_alloc(size_t sz) {
  return frame_allocator_alloc(&frame_allocator, sz);
}

bool running = true;

void terminate() { running = false; }

static struct display *display = NULL;
static bool display_resized = false;
void resized() {
  if (display != NULL) {
    display_resize(display);
  }
  display_resized = true;

  signal(SIGWINCH, resized);
}

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

#define DECLARE_TIMER(timer) struct timespec timer##_begin, timer##_end
#define TIMED_SCOPE_BEGIN(timer) clock_gettime(CLOCK_MONOTONIC, &timer##_begin)
#define TIMED_SCOPE_END(timer) clock_gettime(CLOCK_MONOTONIC, &timer##_end)

struct watched_file {
  uint32_t watch_id;
  struct buffer *buffer;
};

VEC(struct watched_file) g_watched_files;

void watch_file(struct buffer *buffer, void *userdata) {
  if (buffer_is_backed(buffer)) {
    struct reactor *reactor = (struct reactor *)userdata;
    VEC_APPEND(&g_watched_files, struct watched_file * w);
    w->buffer = buffer;
    w->watch_id = reactor_watch_file(reactor, buffer->filename, FileWritten);
  }
}

void reload_buffer(struct buffer *buffer) {
  if (!buffer_is_modified(buffer)) {
    buffer_reload(buffer);
  } else {
    minibuffer_echo("not updating buffer %s because it contains changes",
                    buffer->name);
  }
}

void update_file_watches(struct reactor *reactor) {
  // first, find invalid file watches and try to update them
  VEC_FOR_EACH(&g_watched_files, struct watched_file * w) {
    if (w->watch_id == -1) {
      w->watch_id =
          reactor_watch_file(reactor, w->buffer->filename, FileWritten);
      reload_buffer(w->buffer);
    }
  }

  // then pick up any events we might have
  struct file_event ev;
  while (reactor_next_file_event(reactor, &ev)) {
    // find the buffer we need to reload
    VEC_FOR_EACH(&g_watched_files, struct watched_file * w) {
      if (w->watch_id == ev.id) {
        if (ev.mask & LastEvent != 0) {
          w->watch_id = -1;
          continue;
        }

        reload_buffer(w->buffer);
        break;
      }
    }
  }
}

void usage() {
  printf("dged - a text editor for datagubbar/datagummor!\n");
  printf("usage: dged [-l/--line line_number] [-e/--end] [-h/--help] "
         "[filename]\n");
}

int main(int argc, char *argv[]) {

  static struct option longopts[] = {{"line", required_argument, NULL, 'l'},
                                     {"end", no_argument, NULL, 'e'},
                                     {"help", no_argument, NULL, 'h'},
                                     {NULL, 0, NULL, 0}};

  char *filename = NULL;
  uint32_t jumpline = 1;
  bool goto_end = false;
  char ch;
  while ((ch = getopt_long(argc, argv, "hel:", longopts, NULL)) != -1) {
    switch (ch) {
    case 'l':
      jumpline = atoi(optarg);
      break;
    case 'e':
      goto_end = true;
      break;
    case 'h':
      usage();
      return 0;
      break;
    default:
      usage();
      return 1;
    }
  }
  argc -= optind;
  argv += optind;

  if (argc > 1) {
    fprintf(stderr, "More than one file to open is not supported\n");
    return 2;
  } else if (argc == 1) {
    filename = strdup(argv[0]);
  }

  setlocale(LC_ALL, "");

  signal(SIGTERM, terminate);

  struct commands commands = command_registry_create(32);

  settings_init(64);
  const char *config_path = getenv("XDG_CONFIG_HOME");
  if (config_path == NULL) {
    config_path = "~/.config";
  }
  char settings_file[1024];
  snprintf(settings_file, 1024, "%s/dged/dged.toml", config_path);
  char *settings_file_abs = expanduser(settings_file);
  char **errmsgs = NULL;
  if (access(settings_file_abs, F_OK) == 0) {
    int32_t ret = settings_from_file(settings_file_abs, &errmsgs);
    if (ret > 0) {
      fprintf(stderr, "Error reading settings from %s:\n", settings_file_abs);
      for (uint32_t erri = 0; erri < ret; ++erri) {
        fprintf(stderr, "  - %s", errmsgs[erri]);
        free(errmsgs[erri]);
      }
      free(errmsgs);
      free(settings_file_abs);
      return 3;
    } else if (ret < 0) {
      fprintf(stderr, "Error occured reading settings from %s:\n",
              settings_file_abs);
      free(settings_file_abs);
      return 2;
    }
  }

  free(settings_file_abs);

  languages_init(true);
  buffer_static_init();

  frame_allocator = frame_allocator_create(16 * 1024 * 1024);

  struct reactor *reactor = reactor_create();

  display = display_create();
  display_clear(display);
  signal(SIGWINCH, resized);

  register_global_commands(&commands, terminate);
  register_buffer_commands(&commands);
  register_window_commands(&commands);
  register_settings_commands(&commands);

  struct keyboard kbd = keyboard_create(reactor);

  struct keymap *current_keymap = NULL;
  struct keymap *global_keymap = register_bindings();

  VEC_INIT(&g_watched_files, 32);

  struct buffers buflist = {0};
  buffers_init(&buflist, 32);
  buffers_add_add_hook(&buflist, watch_file, (void *)reactor);
  struct buffer initial_buffer = buffer_create("welcome");
  if (filename != NULL) {
    buffer_destroy(&initial_buffer);
    initial_buffer = buffer_from_file(filename);
  } else {
    const char *welcome_txt = "Welcome to the editor for datagubbar 👴\n";
    buffer_set_text(&initial_buffer, (uint8_t *)welcome_txt,
                    strlen(welcome_txt));
  }

  struct buffer *ib = buffers_add(&buflist, initial_buffer);
  struct buffer minibuffer = buffer_create("minibuffer");
  minibuffer_init(&minibuffer);
  reset_minibuffer_keys(&minibuffer);

  windows_init(display_height(display), display_width(display), ib,
               &minibuffer);
  struct window *active = windows_get_active();
  if (goto_end) {
    buffer_goto_end(window_buffer_view(active));
  } else {
    buffer_goto(window_buffer_view(active), jumpline > 0 ? jumpline - 1 : 0, 0);
  }

  DECLARE_TIMER(buffer);
  DECLARE_TIMER(display);
  DECLARE_TIMER(keyboard);

  uint64_t frame_time = 0;
  static char keyname[64] = {0};
  static uint32_t nkeychars = 0;

  while (running) {

    if (display_resized) {
      windows_resize(display_height(display), display_width(display));
      display_resized = false;
    }

    /* Update all windows together with the buffers in them. */
    TIMED_SCOPE_BEGIN(buffer);
    windows_update(frame_alloc, frame_time);
    TIMED_SCOPE_END(buffer);

    struct window *active_window = windows_get_active();
    if (minibuffer_focused()) {
      active_window = minibuffer_window();
    }

    /* Update the screen by flushing command lists collected from updating the
     * buffers.
     */
    TIMED_SCOPE_BEGIN(display);
    display_begin_render(display);
    windows_render(display);
    struct buffer_location cursor =
        window_absolute_cursor_location(active_window);
    display_move_cursor(display, cursor.line, cursor.col);
    display_end_render(display);
    TIMED_SCOPE_END(display);

    /* This blocks for events, so if nothing has happened we block here and let
     * the CPU do something more useful than updating this narcissistic editor.
     * This is also the reason that there is no timed scope around this, it
     * simply makes no sense.
     */
    reactor_update(reactor);

    TIMED_SCOPE_BEGIN(keyboard);
    struct keyboard_update kbd_upd =
        keyboard_update(&kbd, reactor, frame_alloc);

    uint32_t input_data_idx = 0;
    for (uint32_t ki = 0; ki < kbd_upd.nkeys; ++ki) {
      struct key *k = &kbd_upd.keys[ki];

      struct lookup_result res = {.found = false};
      if (current_keymap != NULL) {
        res = lookup_key(current_keymap, 1, k, &commands);
      } else {
        // check the global keymap first, then the buffer one
        res = lookup_key(global_keymap, 1, k, &commands);
        if (!res.found) {
          res = lookup_key(buffer_keymap(window_buffer(active_window)), 1, k,
                           &commands);
        }
      }

      if (res.found) {
        switch (res.type) {
        case BindingType_Command: {
          if (res.command == NULL) {
            minibuffer_echo_timeout(
                4, "binding found for key %s but not command", k);
          } else {
            int32_t ec = execute_command(res.command, &commands, active_window,
                                         &buflist, 0, NULL);
            if (ec != 0 && !minibuffer_displaying()) {
              minibuffer_echo_timeout(4, "command %s failed with exit code %d",
                                      res.command->name, ec);
            }
          }
          current_keymap = NULL;
          nkeychars = 0;
          keyname[0] = '\0';
          break;
        }
        case BindingType_Keymap: {
          if (nkeychars > 0 && nkeychars < 64) {
            keyname[nkeychars] = '-';
            ++nkeychars;
          }

          if (nkeychars < 64) {
            nkeychars += key_name(k, keyname + nkeychars, 64 - nkeychars);
            minibuffer_echo("%s", keyname);
          }

          current_keymap = res.keymap;
          break;
        }
        }
      } else if (k->mod == 0) {
        buffer_add_text(window_buffer_view(active_window),
                        &kbd_upd.raw[k->start], k->end - k->start);
      } else {
        char keyname[16];
        key_name(k, keyname, 16);
        if (current_keymap == NULL) {
          minibuffer_echo_timeout(4, "key \"%s\" is not bound!", keyname);
        } else {
          minibuffer_echo_timeout(4, "key \"%s %s\" is not bound!",
                                  current_keymap->name, keyname);
        }
        current_keymap = NULL;
        nkeychars = 0;
        keyname[0] = '\0';
      }
    }
    TIMED_SCOPE_END(keyboard);

    update_file_watches(reactor);

    // calculate frame time
    struct timespec timers[] = {buffer_begin, buffer_end,     display_begin,
                                display_end,  keyboard_begin, keyboard_end};
    frame_time = calc_frame_time_ns(timers, 3);
    frame_allocator_clear(&frame_allocator);
  }

  windows_destroy();
  minibuffer_destroy();
  buffer_destroy(&minibuffer);
  buffers_destroy(&buflist);
  display_clear(display);
  display_destroy(display);
  destroy_keymaps();
  command_registry_destroy(&commands);
  reactor_destroy(reactor);
  frame_allocator_destroy(&frame_allocator);
  buffer_static_teardown();
  settings_destroy();

  VEC_DESTROY(&g_watched_files);

  return 0;
}

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
#include "dged/buffer_view.h"
#include "dged/buffers.h"
#include "dged/display.h"
#include "dged/lang.h"
#include "dged/minibuffer.h"
#include "dged/path.h"
#include "dged/reactor.h"
#include "dged/settings.h"
#include "dged/timers.h"

#ifdef SYNTAX_ENABLE
#include "dged/syntax.h"

#define xstr(s) str(s)
#define str(s) #s
#endif

#include "bindings.h"
#include "cmds.h"
#include "completion.h"

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

void segfault() {
  // make an effort to restore the
  // terminal to its former glory
  if (display != NULL) {
    display_clear(display);
    display_destroy(display);
  }

  printf("Segfault encountered...\n");
  abort();
}

#define INVALID_WATCH -1
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
    if (w->watch_id == INVALID_WATCH) {
      message("re-watching: %s", w->buffer->filename);
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
        if ((ev.mask & LastEvent) != 0) {
          message("lost watched file: %s", w->buffer->filename);
          w->watch_id = INVALID_WATCH;
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

  const char *filename = NULL;
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
  signal(SIGSEGV, segfault);

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

  struct keyboard kbd = keyboard_create(reactor);

  VEC_INIT(&g_watched_files, 32);

  struct buffers buflist = {0};
  buffers_init(&buflist, 32);
  struct buffer minibuffer = buffer_create("minibuffer");
  minibuffer.lazy_row_add = false;
  minibuffer_init(&minibuffer, &buflist);

  buffers_add_add_hook(&buflist, watch_file, (void *)reactor);

#ifdef SYNTAX_ENABLE
  char *treesitter_path_env = getenv("TREESITTER_GRAMMARS");
  struct setting *path_setting = settings_get("editor.grammars-path");
  char *settings_path = NULL;
  if (path_setting != NULL && path_setting->value.type == Setting_String) {
    settings_path = path_setting->value.string_value;
  }
  const char *builtin_path = join_path(xstr(DATADIR), "grammars");

  const char *treesitter_path[256] = {0};
  uint32_t treesitter_path_len = 0;

  if (treesitter_path_env != NULL) {
    treesitter_path_env = strdup(treesitter_path_env);
    char *result = strtok(treesitter_path_env, ":");
    while (result != NULL && treesitter_path_len < 256) {
      treesitter_path[treesitter_path_len] = result;
      ++treesitter_path_len;
      result = strtok(NULL, ":");
    }
  }

  if (settings_path != NULL) {
    settings_path = strdup(settings_path);
    char *result = strtok(settings_path, ":");
    while (result != NULL && treesitter_path_len < 256) {
      treesitter_path[treesitter_path_len] = result;
      ++treesitter_path_len;
      result = strtok(NULL, ":");
    }
  }

  if (treesitter_path_len < 256) {
    treesitter_path[treesitter_path_len] = builtin_path;
    ++treesitter_path_len;
  }

  syntax_init(treesitter_path_len, treesitter_path);

  if (treesitter_path_env != NULL) {
    free((void *)treesitter_path_env);
  }
  if (settings_path != NULL) {
    free((void *)settings_path);
  }
  free((void *)builtin_path);
#endif

  struct buffer initial_buffer = buffer_create("welcome");
  if (filename != NULL) {
    buffer_destroy(&initial_buffer);
    const char *absfile = to_abspath(filename);
    initial_buffer = buffer_from_file(absfile);
    free((void *)filename);
    free((void *)absfile);
  } else {
    const char *welcome_txt =
        "Welcome to the editor for datagubbar and datagummor 👴👵\n";
    buffer_set_text(&initial_buffer, (uint8_t *)welcome_txt,
                    strlen(welcome_txt));
  }

  struct buffer *ib = buffers_add(&buflist, initial_buffer);

  windows_init(display_height(display), display_width(display), ib,
               &minibuffer);
  struct window *active = windows_get_active();
  if (goto_end) {
    buffer_view_goto_end(window_buffer_view(active));
  } else {
    struct location to = {
        .line = jumpline > 0 ? jumpline - 1 : 0,
        .col = 0,
    };
    buffer_view_goto(window_buffer_view(active), to);
  }

  register_global_commands(&commands, terminate);
  register_buffer_commands(&commands);
  register_window_commands(&commands);
  register_settings_commands(&commands);

  struct keymap *current_keymap = NULL;
  init_bindings();

  init_completion(&buflist);
  timers_init();

  float frame_time = 0.f;
  static char keyname[64] = {0};
  static uint32_t nkeychars = 0;

  while (running) {
    timers_start_frame();
    if (display_resized) {
      windows_resize(display_height(display), display_width(display));
      display_resized = false;
    }

    /* Update all windows together with the buffers in them. */
    struct timer *update_windows = timer_start("update-windows");
    windows_update(frame_alloc, frame_time);
    timer_stop(update_windows);

    struct window *active_window = windows_get_active();

    /* Update the screen by flushing command lists collected
     * from updating the buffers.
     */
    struct timer *update_display = timer_start("display");
    display_begin_render(display);
    windows_render(display);
    struct buffer_view *view = window_buffer_view(active_window);
    struct location cursor = buffer_view_dot_to_visual(view);
    struct window_position winpos = window_position(active_window);
    display_move_cursor(display, winpos.y + cursor.line, winpos.x + cursor.col);
    display_end_render(display);
    timer_stop(update_display);

    /* This blocks for events, so if nothing has happened we block here and let
     * the CPU do something more useful than updating this editor for no reason.
     * This is also the reason that there is no timed scope around this, it
     * simply makes no sense.
     */
    reactor_update(reactor);

    struct timer *update_keyboard = timer_start("update-keyboard");
    struct keyboard_update kbd_upd =
        keyboard_update(&kbd, reactor, frame_alloc);

    uint32_t input_data_idx = 0;
    for (uint32_t ki = 0; ki < kbd_upd.nkeys; ++ki) {
      struct key *k = &kbd_upd.keys[ki];

      struct lookup_result res = {.found = false};
      if (current_keymap != NULL) {
        res = lookup_key(current_keymap, 1, k, &commands);
      } else {
        struct keymap *buffer_maps[128];
        uint32_t nkeymaps =
            buffer_keymaps(window_buffer(active_window), buffer_maps, 128);
        for (uint32_t kmi = nkeymaps; kmi > 0; --kmi) {
          res = lookup_key(buffer_maps[kmi - 1], 1, k, &commands);
          if (res.found) {
            break;
          }
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
        buffer_view_add(window_buffer_view(active_window),
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
    timer_stop(update_keyboard);

    update_file_watches(reactor);

    // calculate frame time
    frame_time = timer_average(update_windows) +
                 timer_average(update_keyboard) + timer_average(update_display);

    frame_allocator_clear(&frame_allocator);
  }

  timers_destroy();
  destroy_completion();
  windows_destroy();
  minibuffer_destroy();
  buffer_destroy(&minibuffer);
  buffers_destroy(&buflist);

#ifdef SYNTAX_ENABLE
  syntax_teardown();
#endif

  display_clear(display);
  display_destroy(display);
  destroy_bindings();
  command_registry_destroy(&commands);
  reactor_destroy(reactor);
  frame_allocator_destroy(&frame_allocator);
  buffer_static_teardown();
  settings_destroy();

  VEC_DESTROY(&g_watched_files);

  return 0;
}

#include <errno.h>
#include <getopt.h>
#include <locale.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "config.h"

#include "dged/allocator.h"
#include "dged/binding.h"
#include "dged/buffer.h"
#include "dged/buffer_view.h"
#include "dged/buffers.h"
#include "dged/command.h"
#include "dged/display.h"
#include "dged/lang.h"
#include "dged/minibuffer.h"
#include "dged/path.h"
#include "dged/reactor.h"
#include "dged/s8.h"
#include "dged/settings.h"
#include "dged/timers.h"
#include "dged/window.h"

#ifdef SYNTAX_ENABLE
#include "dged/syntax.h"

#define xstr(s) str(s)
#define str(s) #s
#endif

#ifdef LSP_ENABLE
#include "lsp.h"
#endif

#include "bindings.h"
#include "cmds.h"
#include "completion.h"
#include "frame-hooks.h"
#include "version.h"

/* welcome.h is generated from welcome.inc with
 * xxd -n welcome_text -i <path-to-welcome.inc> <path-to-welcome.h>
 * or similar.
 */
#include "welcome.h"

static struct frame_allocator frame_allocator;

void *frame_alloc(size_t sz) {
  return frame_allocator_alloc(&frame_allocator, sz);
}

static bool running = true;

void terminate(void) { running = false; }
void terminate2(int sig) {
  (void)sig;
  running = false;
}

static struct display *display = NULL;
static bool display_resized = false;
void resized(int sig) {
  (void)sig;
  if (display != NULL) {
    display_resize(display);
  }
  display_resized = true;

  signal(SIGWINCH, resized);
}

void terminal_stop(int sig) {
  (void)sig;
  if (display != NULL) {
    display_clear(display);
    display_restore(display);
  }

#ifdef SIGSTOP
  kill(0, SIGSTOP);
#endif
}

void suspend() { terminal_stop(0); }

void resume(int sig) {
  (void)sig;
  display_initialize(display);
}

void handle_crash(int sig) {
  (void)sig;

  fprintf(stderr,
          "Crash encountered - waiting for debugger (PID: %d, cancel with "
          "C-c)...\n",
          getpid());

  static bool waiting = true;
  while (waiting) {
    sleep(1);
  }

  // make an effort to restore the
  // terminal to its former glory
  if (display != NULL) {
    display_clear(display);
    display_destroy(display);
  }
}

#define INVALID_WATCH (uint32_t) - 1

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
  // first, pick up any events we might have
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

  // then, find invalid file watches and try to update them
  VEC_FOR_EACH(&g_watched_files, struct watched_file * w) {
    if (w->watch_id == INVALID_WATCH) {
      w->watch_id =
          reactor_watch_file(reactor, w->buffer->filename, FileWritten);

      if (w->watch_id != INVALID_WATCH) {
        reload_buffer(w->buffer);
      }
    }
  }
}

static void usage(void) {
  printf("dged - a text editor for datagubbar/datagummor!\n");
  printf("usage: dged [-l/--line line_number] [-e/--end] [-h/--help] "
         "[filename]\n");
}

static void version(void) {
  printf("dged - %s\n© Albert Cervin 2024\n", DGED_VERSION);
}

int main(int argc, char *argv[]) {

  static struct option longopts[] = {{"line", required_argument, NULL, 'l'},
                                     {"end", no_argument, NULL, 'e'},
                                     {"version", no_argument, NULL, 'V'},
                                     {"help", no_argument, NULL, 'h'},
                                     {NULL, 0, NULL, 0}};

  const char *filename = NULL;
  uint32_t jumpline = 1;
  bool goto_end = false;
  char ch;
  while ((ch = getopt_long(argc, argv, "Vhel:", longopts, NULL)) != -1) {
    switch (ch) {
    case 'V':
      version();
      return 0;
      break;
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

  signal(SIGTERM, terminate2);

  struct sigaction crash = {};
  crash.sa_flags |= SA_RESETHAND;
  crash.sa_handler = handle_crash;
  sigaction(SIGSEGV, &crash, NULL);
  sigaction(SIGABRT, &crash, NULL);

#ifdef SIGTSTP
  struct sigaction tstop = {};
  sigfillset(&tstop.sa_mask);
  tstop.sa_handler = terminal_stop;
  sigaction(SIGTSTP, &tstop, NULL);
#endif

#ifdef SIGCONT
  struct sigaction cont = {};
  sigfillset(&cont.sa_mask);
  cont.sa_handler = resume;
  sigaction(SIGCONT, &cont, NULL);
#endif

  struct commands commands = command_registry_create(32);

  settings_init(64);
  const char *config_path = getenv("XDG_CONFIG_HOME");
  if (config_path == NULL) {
    config_path = "~/.config";
  }
  struct s8 settings_file = s8from_fmt("%s/dged/dged.toml", config_path);
  struct s8 settings_file_abs = expanduser(settings_file);
  char **errmsgs = NULL;
  if (access(s8ascstr(settings_file_abs), F_OK) == 0) {
    int32_t ret = settings_from_file(s8ascstr(settings_file_abs), &errmsgs);
    if (ret > 0) {
      fprintf(stderr, "Error reading settings from %s:\n",
              s8ascstr(settings_file_abs));
      uint32_t nerrors = (uint32_t)ret;
      for (uint32_t erri = 0; erri < nerrors; ++erri) {
        fprintf(stderr, "  - %s", errmsgs[erri]);
        free(errmsgs[erri]);
      }
      free(errmsgs);

      s8delete(settings_file);
      s8delete(settings_file_abs);
      return 3;
    } else if (ret < 0) {
      fprintf(stderr, "Error occured reading settings from %s:\n",
              s8ascstr(settings_file_abs));
      s8delete(settings_file);
      s8delete(settings_file_abs);
      return 2;
    }
  }

  s8delete(settings_file);
  s8delete(settings_file_abs);

  languages_init(true);
  buffer_static_init();

  frame_allocator = frame_allocator_create(16 * 1024 * 1024);
  struct frame_allocator program_allocator =
      frame_allocator_create(16 * 1024 * 1024);

  struct reactor *reactor = reactor_create();
  if (reactor == NULL) {
    fprintf(stderr, "Failed to create event reactor: %s\n", strerror(errno));
    return 8;
  }

  display = display_create();
  if (display == NULL) {
    fprintf(stderr, "Failed to set up display: %s\n", strerror(errno));
    return 9;
  }

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

  init_bindings();

  init_completion(&buflist);

#ifdef SYNTAX_ENABLE
  char *treesitter_path_env = getenv("TREESITTER_GRAMMARS");
  struct setting *path_setting = settings_get("editor.grammars-path");
  char *settings_path = NULL;
  if (path_setting != NULL && path_setting->value.type == Setting_String) {
    settings_path = path_setting->value.data.string_value;
  }
  struct s8 builtin_path = join_path(s8(xstr(DATADIR)), s8("grammars"));

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
    treesitter_path[treesitter_path_len] = s8ascstr(builtin_path);
    ++treesitter_path_len;
  }

  syntax_init(treesitter_path_len, treesitter_path);

  if (treesitter_path_env != NULL) {
    free((void *)treesitter_path_env);
  }
  if (settings_path != NULL) {
    free((void *)settings_path);
  }
  s8delete(builtin_path);
#endif

#ifdef LSP_ENABLE
  lang_servers_init(reactor, &buflist, &commands);
#endif

  struct buffer *welcome_buffer =
      buffers_add(&buflist, buffer_create("*welcome*"));
  welcome_buffer->force_show_ws_off = true;
  welcome_buffer->lazy_row_add = false;
  buffer_set_readonly(welcome_buffer, true);
  buffer_set_text(welcome_buffer, (uint8_t *)welcome_text, welcome_text_len);

  register_global_commands(&commands, terminate, suspend, &program_allocator);
  register_buffer_commands(&commands);
  register_window_commands(&commands);
  register_settings_commands(&commands);

  struct keymap *current_keymap = NULL;
  timers_init();
  init_frame_hooks();

  windows_init(display_height(display), display_width(display), welcome_buffer,
               &minibuffer, &buflist);
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

  float frame_time = 0.f;
  bool needs_render = true;
  uint64_t last_render_ns = 0;
  const uint64_t target_render_ns = 5 * 1e6 /* 5 ms */;
  uint64_t rendered_frames = 0;

  // load initial buffer from command line
  if (filename != NULL) {
    struct command *cmd = lookup_command(&commands, "find-file");
    if (cmd != NULL) {
      const char *av[] = {filename};
      execute_command(cmd, &commands, windows_get_active(), &buflist, 1, av);
    }
    free((void *)filename);
  }

  while (running) {
    timers_start_frame();
    if (display_resized) {
      windows_resize(display_height(display), display_width(display));
      display_resized = false;
      needs_render = true;
    }

    if (buffers_num_buffers(&buflist) == 0) {
      running = false;
      continue;
    }

    /* Update all windows together with the buffers in them. */
    struct timer *update_windows = timer_start("update-windows");
    needs_render |= windows_update(frame_alloc, frame_time, rendered_frames);
    timer_stop(update_windows);

    struct window *active_window = windows_get_active();

    /* Update the screen by flushing command lists collected
     * from updating the buffers.
     */
    struct timer *update_display = timer_start("display");
    int64_t time_to_render_ns =
        (last_render_ns + target_render_ns) - instant_ns();
    if (needs_render && time_to_render_ns < 0) {
      display_begin_render(display);
      windows_render(display);
      struct buffer_view *view = window_buffer_view(active_window);
      struct location cursor = buffer_view_dot_to_visual(view);
      struct window_position winpos = window_position(active_window);
      display_move_cursor(display, winpos.y + cursor.line,
                          winpos.x + cursor.col);
      display_end_render(display);
      needs_render = false;
      ++rendered_frames;
      last_render_ns = instant_ns();
    }
    timer_stop(update_display);

    /* if we have dispatched frame hooks, they need a
     * full cycle of updates.
     */
    if (dispatch_next_frame_hooks() == 0) {
      /* This blocks for events, so if nothing has happened we block here and
       * let the CPU do something more useful than updating this editor for no
       * reason. This is also the reason that there is no timed scope around
       * this, it simply makes no sense.
       */
      reactor_update(reactor,
                     needs_render ? ((time_to_render_ns + 1e6 - 1) / 1e6) : -1);
    }

    struct timer *update_keyboard = timer_start("update-keyboard");
    struct keyboard_update kbd_upd =
        keyboard_update(&kbd, reactor, frame_alloc);

    needs_render |= kbd_upd.nkeys > 0;
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
          if (res.data.command == NULL) {
            minibuffer_echo_timeout(
                4, "binding found for key %s but not command", k);
          } else {
            int32_t ec = execute_command(res.data.command, &commands,
                                         active_window, &buflist, 0, NULL);
            if (ec != 0 && !minibuffer_displaying()) {
              minibuffer_echo_timeout(4, "command %s failed with exit code %d",
                                      res.data.command->name, ec);
            }
          }
          current_keymap = NULL;
          break;
        }
        case BindingType_Keymap: {
          current_keymap = res.data.keymap;
          abort_completion();
          break;
        }
        }
      } else if (k->mod == 0) {
        // self-inserting chars
        buffer_view_add(window_buffer_view(active_window),
                        &kbd_upd.raw[k->start], k->end - k->start);
      } else {
        char keyname[16];
        key_name(k, keyname, 16);
        if (current_keymap == NULL) {
          minibuffer_display_timeout(4, "key \"%s\" is not bound!", keyname);
        } else {
          minibuffer_display_timeout(4, "key \"%s %s\" is not bound!",
                                     current_keymap->name, keyname);
        }
        current_keymap = NULL;
      }
    }
    timer_stop(update_keyboard);

    update_file_watches(reactor);

#if defined(LSP_ENABLE)
    lang_servers_update();
#endif

    // calculate frame time
    frame_time = timer_average(update_windows) +
                 timer_average(update_keyboard) + timer_average(update_display);

    timers_end_frame();
    frame_allocator_clear(&frame_allocator);
  }

  teardown_frame_hooks();
  timers_destroy();
  teardown_global_commands();
  windows_destroy();

#ifdef SYNTAX_ENABLE
  syntax_teardown();
#endif

#ifdef LSP_ENABLE
  lang_servers_teardown();
#endif

  destroy_completion();
  minibuffer_destroy();
  buffer_destroy(&minibuffer);
  buffers_destroy(&buflist);

  display_clear(display);
  display_destroy(display);
  destroy_bindings();
  command_registry_destroy(&commands);
  reactor_destroy(reactor);
  frame_allocator_destroy(&frame_allocator);
  frame_allocator_destroy(&program_allocator);
  buffer_static_teardown();
  settings_destroy();

  VEC_DESTROY(&g_watched_files);

  return 0;
}

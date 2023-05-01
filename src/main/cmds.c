#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "dged/binding.h"
#include "dged/buffer.h"
#include "dged/buffers.h"
#include "dged/command.h"
#include "dged/minibuffer.h"
#include "dged/settings.h"

#include "bindings.h"

int32_t _abort(struct command_ctx ctx, int argc, const char *argv[]) {
  minibuffer_abort_prompt();
  minibuffer_echo_timeout(4, "💣 aborted");
  return 0;
}

int32_t unimplemented_command(struct command_ctx ctx, int argc,
                              const char *argv[]) {
  minibuffer_echo("TODO: %s is not implemented", (const char *)ctx.userdata);
  return 0;
}

int32_t exit_editor(struct command_ctx ctx, int argc, const char *argv[]) {
  ((void (*)())ctx.userdata)();
  return 0;
}

int32_t find_file(struct command_ctx ctx, int argc, const char *argv[]) {
  const char *pth = NULL;
  if (argc == 0) {
    return minibuffer_prompt(ctx, "find file: ");
  }

  pth = argv[0];
  struct stat sb = {0};
  if (stat(pth, &sb) < 0 && errno != ENOENT) {
    minibuffer_echo("stat on %s failed: %s", pth, strerror(errno));
    return 1;
  }

  if (S_ISDIR(sb.st_mode) && errno != ENOENT) {
    minibuffer_echo("TODO: implement dired!");
    return 1;
  }

  window_set_buffer(ctx.active_window,
                    buffers_add(ctx.buffers, buffer_from_file((char *)pth)));
  minibuffer_echo_timeout(4, "buffer \"%s\" loaded",
                          window_buffer(ctx.active_window)->name);

  return 0;
}

int32_t write_file(struct command_ctx ctx, int argc, const char *argv[]) {
  const char *pth = NULL;
  if (argc == 0) {
    return minibuffer_prompt(ctx, "write to file: ");
  }

  pth = argv[0];
  buffer_write_to(window_buffer(ctx.active_window), pth);

  return 0;
}

int32_t run_interactive(struct command_ctx ctx, int argc, const char *argv[]) {
  if (argc == 0) {
    return minibuffer_prompt(ctx, "execute: ");
  }

  struct command *cmd = lookup_command(ctx.commands, argv[0]);
  if (cmd != NULL) {
    return execute_command(cmd, ctx.commands, ctx.active_window, ctx.buffers,
                           argc - 1, argv + 1);
  } else {
    minibuffer_echo_timeout(4, "command %s not found", argv[0]);
    return 11;
  }
}

int32_t do_switch_buffer(struct command_ctx ctx, int argc, const char *argv[]) {
  const char *bufname = argv[0];
  if (argc == 0) {
    // switch back to prev buffer
    if (window_has_prev_buffer(ctx.active_window)) {
      bufname = window_prev_buffer(ctx.active_window)->name;
    } else {
      return 0;
    }
  }

  struct buffer *buf = buffers_find(ctx.buffers, bufname);

  if (buf == NULL) {
    minibuffer_echo_timeout(4, "buffer %s not found", bufname);
    return 1;
  } else {
    window_set_buffer(ctx.active_window, buf);
    return 0;
  }
}

static struct command do_switch_buffer_cmd = {.fn = do_switch_buffer,
                                              .name = "do-switch-buffer"};

int32_t switch_buffer(struct command_ctx ctx, int argc, const char *argv[]) {
  if (argc == 0) {
    ctx.self = &do_switch_buffer_cmd;
    if (window_has_prev_buffer(ctx.active_window)) {
      return minibuffer_prompt(ctx, "buffer (default %s): ",
                               window_prev_buffer(ctx.active_window)->name);
    } else {
      return minibuffer_prompt(ctx, "buffer: ");
    }
  }

  return execute_command(&do_switch_buffer_cmd, ctx.commands, ctx.active_window,
                         ctx.buffers, argc, argv);
}

static char *g_last_search = NULL;

int64_t matchdist(struct match *match, struct buffer_location loc) {
  struct buffer_location begin = match->begin;

  int64_t linedist = (int64_t)begin.line - (int64_t)loc.line;
  int64_t coldist = (int64_t)begin.col - (int64_t)loc.col;

  return linedist * linedist + coldist * coldist;
}

int buffer_loc_cmp(struct buffer_location loc1, struct buffer_location loc2) {
  if (loc1.line < loc2.line) {
    return -1;
  } else if (loc1.line > loc2.line) {
    return 1;
  } else {
    if (loc1.col < loc2.col) {
      return -1;
    } else if (loc1.col > loc2.col) {
      return 1;
    } else {
      return 0;
    }
  }
}

const char *search_prompt(bool reverse) {
  const char *txt = "search (down): ";
  if (reverse) {
    txt = "search (up): ";
  }

  return txt;
}

void do_search(struct buffer_view *view, const char *pattern, bool reverse) {
  struct match *matches = NULL;
  uint32_t nmatches = 0;

  g_last_search = strdup(pattern);

  struct buffer_view *buffer_view = window_buffer_view(windows_get_active());
  buffer_find(buffer_view->buffer, pattern, &matches, &nmatches);

  // find the "nearest" match
  if (nmatches > 0) {
    struct match *closest = reverse ? &matches[nmatches - 1] : &matches[0];
    int64_t closest_dist = INT64_MAX;
    for (uint32_t matchi = 0; matchi < nmatches; ++matchi) {
      struct match *m = &matches[matchi];
      int res = buffer_loc_cmp(m->begin, view->dot);
      int64_t dist = matchdist(m, view->dot);
      if (((res < 0 && reverse) || (res > 0 && !reverse)) &&
          dist < closest_dist) {
        closest_dist = dist;
        closest = m;
      }
    }
    buffer_goto(buffer_view, closest->begin.line, closest->begin.col);
  }
}

int32_t search_interactive(struct command_ctx ctx, int argc,
                           const char *argv[]) {
  const char *pattern = NULL;
  if (minibuffer_content().nbytes == 0) {
    // recall the last search, if any
    if (g_last_search != NULL) {
      struct buffer_view *view = window_buffer_view(minibuffer_window());
      buffer_clear(view);
      buffer_add_text(view, (uint8_t *)g_last_search, strlen(g_last_search));
      pattern = g_last_search;
    }
  } else {
    struct text_chunk content = minibuffer_content();
    char *p = malloc(content.nbytes + 1);
    memcpy(p, content.text, content.nbytes);
    p[content.nbytes] = '\0';
    pattern = p;
  }

  minibuffer_set_prompt(search_prompt(*(bool *)ctx.userdata));

  if (pattern != NULL) {
    // ctx.active_window would be the minibuffer window
    do_search(window_buffer_view(windows_get_active()), pattern,
              *(bool *)ctx.userdata);
  }
  return 0;
}

static bool search_dir_backward = true;
static bool search_dir_forward = false;
static struct command search_forward_command = {
    .fn = search_interactive,
    .name = "search-forward",
    .userdata = &search_dir_forward,
};

static struct command search_backward_command = {
    .fn = search_interactive,
    .name = "search-backward",
    .userdata = &search_dir_backward,
};

int32_t find(struct command_ctx ctx, int argc, const char *argv[]) {
  bool reverse = strcmp((char *)ctx.userdata, "backward") == 0;
  if (argc == 0) {
    struct binding bindings[] = {
        ANONYMOUS_BINDING(Ctrl, 'S', &search_forward_command),
        ANONYMOUS_BINDING(Ctrl, 'R', &search_backward_command),
    };
    buffer_bind_keys(minibuffer_buffer(), bindings,
                     sizeof(bindings) / sizeof(bindings[0]));
    return minibuffer_prompt(ctx, search_prompt(reverse));
  }

  reset_minibuffer_keys(minibuffer_buffer());
  do_search(window_buffer_view(ctx.active_window), argv[0], reverse);

  return 0;
}

int32_t timers(struct command_ctx ctx, int argc, const char *argv[]) {

  struct buffer *b = buffers_add(ctx.buffers, buffer_create("timers"));
  buffer_set_readonly(b, true);
  struct window *new_window_a, *new_window_b;
  window_split(ctx.active_window, &new_window_a, &new_window_b);

  const char *txt =
      "TODO: this is not real values!\ntimer 1: 1ms\ntimer 2: 2ms\n";
  buffer_set_text(b, (uint8_t *)txt, strlen(txt));

  window_set_buffer(new_window_b, b);
  return 0;
}

void register_global_commands(struct commands *commands,
                              void (*terminate_cb)()) {

  struct command global_commands[] = {
      {.name = "find-file", .fn = find_file},
      {.name = "write-file", .fn = write_file},
      {.name = "run-command-interactive", .fn = run_interactive},
      {.name = "switch-buffer", .fn = switch_buffer},
      {.name = "abort", .fn = _abort},
      {.name = "find-next", .fn = find, .userdata = "forward"},
      {.name = "find-prev", .fn = find, .userdata = "backward"},
      {.name = "timers", .fn = timers},
      {.name = "exit", .fn = exit_editor, .userdata = terminate_cb}};

  register_commands(commands, global_commands,
                    sizeof(global_commands) / sizeof(global_commands[0]));
}

#define BUFFER_WRAPCMD_POS(fn)                                                 \
  static int32_t fn##_cmd(struct command_ctx ctx, int argc,                    \
                          const char *argv[]) {                                \
    fn(window_buffer_view(ctx.active_window));                                 \
    return 0;                                                                  \
  }

#define BUFFER_WRAPCMD(fn)                                                     \
  static int32_t fn##_cmd(struct command_ctx ctx, int argc,                    \
                          const char *argv[]) {                                \
    fn(window_buffer(ctx.active_window));                                      \
    return 0;                                                                  \
  }

BUFFER_WRAPCMD_POS(buffer_kill_line);
BUFFER_WRAPCMD_POS(buffer_forward_delete_char);
BUFFER_WRAPCMD_POS(buffer_backward_delete_char);
BUFFER_WRAPCMD_POS(buffer_backward_char);
BUFFER_WRAPCMD_POS(buffer_backward_word);
BUFFER_WRAPCMD_POS(buffer_forward_char);
BUFFER_WRAPCMD_POS(buffer_forward_word);
BUFFER_WRAPCMD_POS(buffer_backward_line);
BUFFER_WRAPCMD_POS(buffer_forward_line);
BUFFER_WRAPCMD_POS(buffer_end_of_line);
BUFFER_WRAPCMD_POS(buffer_beginning_of_line);
BUFFER_WRAPCMD_POS(buffer_newline);
BUFFER_WRAPCMD_POS(buffer_indent);
BUFFER_WRAPCMD(buffer_to_file);
BUFFER_WRAPCMD(buffer_reload);
BUFFER_WRAPCMD_POS(buffer_set_mark);
BUFFER_WRAPCMD_POS(buffer_clear_mark);
BUFFER_WRAPCMD_POS(buffer_copy);
BUFFER_WRAPCMD_POS(buffer_cut);
BUFFER_WRAPCMD_POS(buffer_paste);
BUFFER_WRAPCMD_POS(buffer_paste_older);
BUFFER_WRAPCMD_POS(buffer_goto_beginning);
BUFFER_WRAPCMD_POS(buffer_goto_end);
BUFFER_WRAPCMD_POS(buffer_undo);
static int32_t buffer_view_scroll_up_cmd(struct command_ctx ctx, int argc,
                                         const char *argv[]) {
  buffer_view_scroll_up(window_buffer_view(ctx.active_window),
                        window_height(ctx.active_window));
  return 0;
};
static int32_t buffer_view_scroll_down_cmd(struct command_ctx ctx, int argc,
                                           const char *argv[]) {
  buffer_view_scroll_down(window_buffer_view(ctx.active_window),
                          window_height(ctx.active_window));
  return 0;
};

void register_buffer_commands(struct commands *commands) {

  static struct command buffer_commands[] = {
      {.name = "kill-line", .fn = buffer_kill_line_cmd},
      {.name = "delete-char", .fn = buffer_forward_delete_char_cmd},
      {.name = "backward-delete-char", .fn = buffer_backward_delete_char_cmd},
      {.name = "backward-char", .fn = buffer_backward_char_cmd},
      {.name = "backward-word", .fn = buffer_backward_word_cmd},
      {.name = "forward-char", .fn = buffer_forward_char_cmd},
      {.name = "forward-word", .fn = buffer_forward_word_cmd},
      {.name = "backward-line", .fn = buffer_backward_line_cmd},
      {.name = "forward-line", .fn = buffer_forward_line_cmd},
      {.name = "end-of-line", .fn = buffer_end_of_line_cmd},
      {.name = "beginning-of-line", .fn = buffer_beginning_of_line_cmd},
      {.name = "newline", .fn = buffer_newline_cmd},
      {.name = "indent", .fn = buffer_indent_cmd},
      {.name = "buffer-write-to-file", .fn = buffer_to_file_cmd},
      {.name = "set-mark", .fn = buffer_set_mark_cmd},
      {.name = "clear-mark", .fn = buffer_clear_mark_cmd},
      {.name = "copy", .fn = buffer_copy_cmd},
      {.name = "cut", .fn = buffer_cut_cmd},
      {.name = "paste", .fn = buffer_paste_cmd},
      {.name = "paste-older", .fn = buffer_paste_older_cmd},
      {.name = "goto-beginning", .fn = buffer_goto_beginning_cmd},
      {.name = "goto-end", .fn = buffer_goto_end_cmd},
      {.name = "undo", .fn = buffer_undo_cmd},
      {.name = "scroll-down", .fn = buffer_view_scroll_down_cmd},
      {.name = "scroll-up", .fn = buffer_view_scroll_up_cmd},
      {.name = "reload", .fn = buffer_reload_cmd},
  };

  register_commands(commands, buffer_commands,
                    sizeof(buffer_commands) / sizeof(buffer_commands[0]));
}

static int32_t window_close_cmd(struct command_ctx ctx, int argc,
                                const char *argv[]) {
  window_close(ctx.active_window);
  return 0;
}

static int32_t window_split_cmd(struct command_ctx ctx, int argc,
                                const char *argv[]) {
  struct window *resa, *resb;
  window_split(ctx.active_window, &resa, &resb);
  return 0;
}

static int32_t window_hsplit_cmd(struct command_ctx ctx, int argc,
                                 const char *argv[]) {
  struct window *resa, *resb;
  window_hsplit(ctx.active_window, &resa, &resb);
  return 0;
}

static int32_t window_vsplit_cmd(struct command_ctx ctx, int argc,
                                 const char *argv[]) {
  struct window *resa, *resb;
  window_vsplit(ctx.active_window, &resa, &resb);
  return 0;
}

static int32_t window_close_others_cmd(struct command_ctx ctx, int argc,
                                       const char *argv[]) {
  window_close_others(ctx.active_window);
  return 0;
}

static int32_t window_focus_next_cmd(struct command_ctx ctx, int argc,
                                     const char *argv[]) {
  windows_focus_next();
  return 0;
}

static int32_t window_focus_cmd(struct command_ctx ctx, int argc,
                                const char *argv[]) {
  if (argc == 0) {
    return minibuffer_prompt(ctx, "window id: ");
  }

  if (argc == 1) {
    uint32_t req_id = atoi(argv[0]);
    windows_focus(req_id);
  }

  return 0;
}

static int32_t window_focus_n_cmd(struct command_ctx ctx, int argc,
                                  const char *argv[]) {
  char *window_id = (char *)ctx.userdata;
  const char *argv_[] = {window_id};
  return window_focus_cmd(ctx, 1, argv_);
}

void register_window_commands(struct commands *commands) {
  static struct command window_commands[] = {
      {.name = "window-close", .fn = window_close_cmd},
      {.name = "window-close-others", .fn = window_close_others_cmd},
      {.name = "window-split", .fn = window_split_cmd},
      {.name = "window-split-vertical", .fn = window_vsplit_cmd},
      {.name = "window-split-horizontal", .fn = window_hsplit_cmd},
      {.name = "window-focus-next", .fn = window_focus_next_cmd},
      {.name = "window-focus", .fn = window_focus_cmd},
      {.name = "window-focus-0", .fn = window_focus_n_cmd, .userdata = "0"},
      {.name = "window-focus-1", .fn = window_focus_n_cmd, .userdata = "1"},
      {.name = "window-focus-2", .fn = window_focus_n_cmd, .userdata = "2"},
      {.name = "window-focus-3", .fn = window_focus_n_cmd, .userdata = "3"},
      {.name = "window-focus-4", .fn = window_focus_n_cmd, .userdata = "4"},
      {.name = "window-focus-5", .fn = window_focus_n_cmd, .userdata = "5"},
      {.name = "window-focus-6", .fn = window_focus_n_cmd, .userdata = "6"},
      {.name = "window-focus-7", .fn = window_focus_n_cmd, .userdata = "7"},
      {.name = "window-focus-8", .fn = window_focus_n_cmd, .userdata = "8"},
      {.name = "window-focus-9", .fn = window_focus_n_cmd, .userdata = "9"},
  };

  register_commands(commands, window_commands,
                    sizeof(window_commands) / sizeof(window_commands[0]));
}

int32_t settings_set_cmd(struct command_ctx ctx, int argc, const char *argv[]) {
  if (argc == 0) {
    return minibuffer_prompt(ctx, "setting: ");
  } else if (argc == 1) {
    // validate setting here as well for a better experience
    struct setting *setting = settings_get(argv[0]);
    if (setting == NULL) {
      minibuffer_echo_timeout(4, "no such setting \"%s\"", argv[0]);
      return 1;
    }

    command_ctx_push_arg(&ctx, argv[0]);
    return minibuffer_prompt(ctx, "value: ");
  }

  struct setting *setting = settings_get(argv[0]);
  if (setting == NULL) {
    minibuffer_echo_timeout(4, "no such setting \"%s\"", argv[0]);
    return 1;
  } else {
    const char *value = argv[1];
    struct setting_value new_value = {.type = setting->value.type};
    switch (setting->value.type) {
    case Setting_Bool:
      new_value.bool_value = strncmp("true", value, 4) == 0 ||
                             strncmp("yes", value, 3) == 0 ||
                             strncmp("on", value, 2) == 0;
      break;
    case Setting_Number:
      new_value.number_value = atol(value);
      break;
    case Setting_String:
      new_value.string_value = (char *)value;
      break;
    }

    setting_set_value(setting, new_value);
  }

  return 0;
}

int32_t settings_get_cmd(struct command_ctx ctx, int argc, const char *argv[]) {
  if (argc == 0) {
    return minibuffer_prompt(ctx, "setting: ");
  }

  struct setting *setting = settings_get(argv[0]);
  if (setting == NULL) {
    minibuffer_echo_timeout(4, "no such setting \"%s\"", argv[0]);
    return 1;
  } else {
    char buf[128];
    setting_to_string(setting, buf, 128);
    minibuffer_echo("%s = %s", argv[0], buf);
  }

  return 0;
}

void register_settings_commands(struct commands *commands) {
  static struct command settings_commands[] = {
      {.name = "set", .fn = settings_set_cmd},
      {.name = "get", .fn = settings_get_cmd},
  };

  register_commands(commands, settings_commands,
                    sizeof(settings_commands) / sizeof(settings_commands[0]));
}

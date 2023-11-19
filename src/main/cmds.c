#define _DEFAULT_SOURCE
#include <dirent.h>
#include <errno.h>
#include <libgen.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "dged/binding.h"
#include "dged/buffer.h"
#include "dged/buffer_view.h"
#include "dged/buffers.h"
#include "dged/command.h"
#include "dged/display.h"
#include "dged/minibuffer.h"
#include "dged/path.h"
#include "dged/settings.h"

#include "bindings.h"
#include "search-replace.h"

static void abort_completion();

int32_t _abort(struct command_ctx ctx, int argc, const char *argv[]) {
  abort_replace();
  abort_completion();
  minibuffer_abort_prompt();
  buffer_view_clear_mark(window_buffer_view(ctx.active_window));
  reset_minibuffer_keys(minibuffer_buffer());
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

struct completion {
  const char *display;
  const char *insert;
  bool complete;
};

uint32_t g_ncompletions = 0;
struct completion g_completions[50] = {0};

static void abort_completion() {
  reset_minibuffer_keys(minibuffer_buffer());
  windows_close_popup();

  for (uint32_t compi = 0; compi < g_ncompletions; ++compi) {
    free((void *)g_completions[compi].display);
    free((void *)g_completions[compi].insert);
  }
  g_ncompletions = 0;
}

int cmp_completions(const void *comp_a, const void *comp_b) {
  struct completion *a = (struct completion *)comp_a;
  struct completion *b = (struct completion *)comp_b;
  return strcmp(a->display, b->display);
}

static bool is_hidden(const char *filename) {
  return filename[0] == '.' && filename[1] != '\0' && filename[1] != '.';
}

static void complete_path(const char *path, struct completion results[],
                          uint32_t nresults_max, uint32_t *nresults) {
  uint32_t n = 0;
  char *p1 = to_abspath(path);
  size_t len = strlen(p1);
  char *p2 = strdup(p1);

  size_t inlen = strlen(path);

  if (len == 0) {
    goto done;
  }

  if (nresults_max == 0) {
    goto done;
  }

  const char *dir = p1;
  const char *file = "";

  // check the input path here since
  // to_abspath removes trailing slashes
  if (path[inlen - 1] != '/') {
    dir = dirname(p1);
    file = basename(p2);
  }

  DIR *d = opendir(dir);
  if (d == NULL) {
    goto done;
  }

  errno = 0;
  while (n < nresults_max) {
    struct dirent *de = readdir(d);
    if (de == NULL && errno != 0) {
      // skip the erroring entry
      errno = 0;
      continue;
    } else if (de == NULL && errno == 0) {
      break;
    }

    switch (de->d_type) {
    case DT_DIR:
    case DT_REG:
    case DT_LNK:
      if (!is_hidden(de->d_name) &&
          (strncmp(file, de->d_name, strlen(file)) == 0 || strlen(file) == 0)) {
        const char *disp = strdup(de->d_name);
        results[n] = (struct completion){
            .display = disp,
            .insert = strdup(disp + strlen(file)),
            .complete = de->d_type == DT_REG,
        };
        ++n;
      }
      break;
    }
  }

  closedir(d);

done:
  free(p1);
  free(p2);

  qsort(results, n, sizeof(struct completion), cmp_completions);
  *nresults = n;
}

void update_completion_buffer(struct buffer *buffer, uint32_t width,
                              uint32_t height, void *userdata) {
  struct buffer_view *view = (struct buffer_view *)userdata;
  struct text_chunk line = buffer_line(buffer, view->dot.line);
  buffer_add_text_property(
      view->buffer, (struct location){.line = view->dot.line, .col = 0},
      (struct location){.line = view->dot.line, .col = line.nchars},
      (struct text_property){.type = TextProperty_Colors,
                             .colors = (struct text_property_colors){
                                 .set_bg = false,
                                 .bg = 0,
                                 .set_fg = true,
                                 .fg = 4,
                             }});

  if (line.allocated) {
    free(line.text);
  }
}

static int32_t goto_completion(struct command_ctx ctx, int argc,
                               const char *argv[]) {
  void (*movement_fn)(struct buffer_view *) =
      (void (*)(struct buffer_view *))ctx.userdata;
  struct buffer *b = buffers_find(ctx.buffers, "*completions*");

  // is it in the popup?
  if (b != NULL && window_buffer(popup_window()) == b) {
    struct buffer_view *v = window_buffer_view(popup_window());
    movement_fn(v);

    if (v->dot.line >= text_num_lines(b->text)) {
      buffer_view_backward_line(v);
    }
  }

  return 0;
}

static int32_t insert_completion(struct command_ctx ctx, int argc,
                                 const char *argv[]) {
  struct buffer *b = buffers_find(ctx.buffers, "*completions*");
  // is it in the popup?
  if (b != NULL && window_buffer(popup_window()) == b) {
    struct buffer_view *cv = window_buffer_view(popup_window());

    if (cv->dot.line < g_ncompletions) {
      char *ins = (char *)g_completions[cv->dot.line].insert;
      bool complete = g_completions[cv->dot.line].complete;
      size_t inslen = strlen(ins);
      buffer_view_add(window_buffer_view(windows_get_active()), ins, inslen);

      if (minibuffer_focused() && complete) {
        minibuffer_execute();
      }

      abort_completion();
    }
  }

  return 0;
}

COMMAND_FN("next-completion", next_completion, goto_completion,
           buffer_view_forward_line);
COMMAND_FN("prev-completion", prev_completion, goto_completion,
           buffer_view_backward_line);
COMMAND_FN("insert-completion", insert_completion, insert_completion, NULL);

static void on_find_file_input(void *userdata) {
  struct buffers *buffers = (struct buffers *)userdata;
  struct text_chunk txt = minibuffer_content();

  struct window *mb = minibuffer_window();
  struct location mb_dot = window_buffer_view(mb)->dot;
  struct window_position mbpos = window_position(mb);

  struct buffer *b = buffers_find(buffers, "*completions*");
  if (b == NULL) {
    b = buffers_add(buffers, buffer_create("*completions*"));
    buffer_add_update_hook(b, update_completion_buffer,
                           (void *)window_buffer_view(popup_window()));
    window_set_buffer_e(popup_window(), b, false, false);
  }

  struct buffer_view *v = window_buffer_view(popup_window());

  char path[1024];
  strncpy(path, txt.text, txt.nbytes);
  path[(txt.nbytes >= 1024 ? 1023 : txt.nbytes)] = '\0';

  for (uint32_t compi = 0; compi < g_ncompletions; ++compi) {
    free((void *)g_completions[compi].display);
    free((void *)g_completions[compi].insert);
  }

  g_ncompletions = 0;
  complete_path(path, g_completions, 50, &g_ncompletions);

  size_t max_width = 0;
  struct location prev_dot = v->dot;

  buffer_clear(v->buffer);
  buffer_view_goto(v, (struct location){.line = 0, .col = 0});
  if (g_ncompletions > 0) {
    for (uint32_t compi = 0; compi < g_ncompletions; ++compi) {
      const char *disp = g_completions[compi].display;
      size_t width = strlen(disp);
      if (width > max_width) {
        max_width = width;
      }
      buffer_view_add(v, (uint8_t *)disp, width);

      // the extra newline feels weird in navigation
      if (compi != g_ncompletions - 1) {
        buffer_view_add(v, (uint8_t *)"\n", 1);
      }
    }

    buffer_view_goto(
        v, (struct location){.line = prev_dot.line, .col = prev_dot.col});
    if (prev_dot.line >= text_num_lines(b->text)) {
      buffer_view_backward_line(v);
    }

    if (!popup_window_visible()) {
      struct binding bindings[] = {
          ANONYMOUS_BINDING(Ctrl, 'N', &next_completion_command),
          ANONYMOUS_BINDING(Ctrl, 'P', &prev_completion_command),
          ANONYMOUS_BINDING(ENTER, &insert_completion_command),
      };
      buffer_bind_keys(minibuffer_buffer(), bindings,
                       sizeof(bindings) / sizeof(bindings[0]));
    }

    uint32_t width = max_width > 2 ? max_width : 4,
             height = g_ncompletions > 10 ? 10 : g_ncompletions;
    windows_show_popup(mbpos.y + mb_dot.line - height, mbpos.x + mb_dot.col,
                       width, height);
  } else {
    abort_completion();
  }

  if (txt.allocated) {
    free(txt.text);
  }
}

int32_t find_file(struct command_ctx ctx, int argc, const char *argv[]) {
  const char *pth = NULL;
  if (argc == 0) {
    return minibuffer_prompt_interactive(ctx, on_find_file_input, ctx.buffers,
                                         "find file: ");
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

  const char *filename = to_abspath(pth);
  struct buffer *b = buffers_find_by_filename(ctx.buffers, filename);
  free((char *)filename);

  if (b == NULL) {
    b = buffers_add(ctx.buffers, buffer_from_file((char *)pth));
  } else {
    buffer_reload(b);
  }

  window_set_buffer(ctx.active_window, b);
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
  buffer_set_filename(window_buffer(ctx.active_window), pth);
  buffer_to_file(window_buffer(ctx.active_window));

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

COMMAND_FN("do-switch-buffer", do_switch_buffer, do_switch_buffer, NULL);

int32_t switch_buffer(struct command_ctx ctx, int argc, const char *argv[]) {
  if (argc == 0) {
    ctx.self = &do_switch_buffer_command;
    if (window_has_prev_buffer(ctx.active_window)) {
      return minibuffer_prompt(ctx, "buffer (default %s): ",
                               window_prev_buffer(ctx.active_window)->name);
    } else {
      return minibuffer_prompt(ctx, "buffer: ");
    }
  }

  return execute_command(&do_switch_buffer_command, ctx.commands,
                         ctx.active_window, ctx.buffers, argc, argv);
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

void buffer_to_list_line(struct buffer *buffer, void *userdata) {
  struct buffer_view *listbuf = (struct buffer_view *)userdata;
  char buf[1024];
  size_t written =
      snprintf(buf, 1024, "%-16s %s\n", buffer->name,
               buffer->filename != NULL ? buffer->filename : "<no-file>");

  if (written > 0) {
    buffer_view_add(listbuf, (uint8_t *)buf, written);
  }
}

int32_t buflist_visit_cmd(struct command_ctx ctx, int argc,
                          const char *argv[]) {
  struct window *w = ctx.active_window;

  struct buffer_view *bv = window_buffer_view(w);
  struct text_chunk text = buffer_line(bv->buffer, bv->dot.line);

  char *end = (char *)memchr(text.text, ' ', text.nbytes);

  if (end != NULL) {
    uint32_t len = end - (char *)text.text;
    char *bufname = (char *)malloc(len + 1);
    strncpy(bufname, text.text, len);
    bufname[len] = '\0';

    struct buffer *target = buffers_find(ctx.buffers, bufname);
    free(bufname);
    if (target != NULL) {
      window_set_buffer(w, target);
    }
  }
  return 0;
}

int32_t buflist_close_cmd(struct command_ctx ctx, int argc,
                          const char *argv[]) {
  window_close(ctx.active_window);
  return 0;
}

void buflist_refresh(struct buffers *buffers, struct buffer_view *target) {
  buffer_set_readonly(target->buffer, false);
  buffer_clear(target->buffer);
  buffers_for_each(buffers, buffer_to_list_line, target);
  buffer_view_goto_beginning(target);
  buffer_set_readonly(target->buffer, true);
}

int32_t buflist_refresh_cmd(struct command_ctx ctx, int argc,
                            const char *argv[]) {
  buflist_refresh(ctx.buffers, window_buffer_view(ctx.active_window));
  return 0;
}

int32_t buffer_list(struct command_ctx ctx, int argc, const char *argv[]) {
  struct buffer *b = buffers_find(ctx.buffers, "*buffers*");
  if (b == NULL) {
    b = buffers_add(ctx.buffers, buffer_create("*buffers*"));
  }

  struct window *w = ctx.active_window;
  window_set_buffer(ctx.active_window, b);

  buflist_refresh(ctx.buffers, window_buffer_view(w));

  static struct command buflist_visit = {
      .name = "buflist-visit",
      .fn = buflist_visit_cmd,
  };

  static struct command buflist_close = {
      .name = "buflist-close",
      .fn = buflist_close_cmd,
  };

  static struct command buflist_refresh = {
      .name = "buflist-refresh",
      .fn = buflist_refresh_cmd,
  };

  struct binding bindings[] = {
      ANONYMOUS_BINDING(Ctrl, 'M', &buflist_visit),
      ANONYMOUS_BINDING(None, 'q', &buflist_close),
      ANONYMOUS_BINDING(None, 'g', &buflist_refresh),
  };
  buffer_bind_keys(b, bindings, sizeof(bindings) / sizeof(bindings[0]));
  windows_set_active(w);

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
      {.name = "timers", .fn = timers},
      {.name = "buffer-list", .fn = buffer_list},
      {.name = "exit", .fn = exit_editor, .userdata = terminate_cb}};

  register_commands(commands, global_commands,
                    sizeof(global_commands) / sizeof(global_commands[0]));

  register_search_replace_commands(commands);
}

#define BUFFER_VIEW_WRAPCMD(fn)                                                \
  static int32_t fn##_cmd(struct command_ctx ctx, int argc,                    \
                          const char *argv[]) {                                \
    buffer_view_##fn(window_buffer_view(ctx.active_window));                   \
    return 0;                                                                  \
  }

#define BUFFER_WRAPCMD(fn)                                                     \
  static int32_t fn##_cmd(struct command_ctx ctx, int argc,                    \
                          const char *argv[]) {                                \
    buffer_##fn(window_buffer(ctx.active_window));                             \
    return 0;                                                                  \
  }

BUFFER_WRAPCMD(to_file);
BUFFER_WRAPCMD(reload);
BUFFER_VIEW_WRAPCMD(kill_line);
BUFFER_VIEW_WRAPCMD(forward_delete_char);
BUFFER_VIEW_WRAPCMD(backward_delete_char);
BUFFER_VIEW_WRAPCMD(forward_delete_word);
BUFFER_VIEW_WRAPCMD(backward_delete_word);
BUFFER_VIEW_WRAPCMD(backward_char);
BUFFER_VIEW_WRAPCMD(backward_word);
BUFFER_VIEW_WRAPCMD(forward_char);
BUFFER_VIEW_WRAPCMD(forward_word);
BUFFER_VIEW_WRAPCMD(backward_line);
BUFFER_VIEW_WRAPCMD(forward_line);
BUFFER_VIEW_WRAPCMD(goto_end_of_line);
BUFFER_VIEW_WRAPCMD(goto_beginning_of_line);
BUFFER_VIEW_WRAPCMD(newline);
BUFFER_VIEW_WRAPCMD(indent);
BUFFER_VIEW_WRAPCMD(set_mark);
BUFFER_VIEW_WRAPCMD(clear_mark);
BUFFER_VIEW_WRAPCMD(copy);
BUFFER_VIEW_WRAPCMD(cut);
BUFFER_VIEW_WRAPCMD(paste);
BUFFER_VIEW_WRAPCMD(paste_older);
BUFFER_VIEW_WRAPCMD(goto_beginning);
BUFFER_VIEW_WRAPCMD(goto_end);
BUFFER_VIEW_WRAPCMD(undo);

static int32_t scroll_up_cmd(struct command_ctx ctx, int argc,
                             const char *argv[]) {
  buffer_view_backward_nlines(window_buffer_view(ctx.active_window),
                              window_height(ctx.active_window) - 1);
  return 0;
};

static int32_t scroll_down_cmd(struct command_ctx ctx, int argc,
                               const char *argv[]) {
  buffer_view_forward_nlines(window_buffer_view(ctx.active_window),
                             window_height(ctx.active_window) - 1);
  return 0;
};

static int32_t goto_line(struct command_ctx ctx, int argc, const char *argv[]) {
  if (argc == 0) {
    return minibuffer_prompt(ctx, "line: ");
  }

  struct buffer_view *v = window_buffer_view(ctx.active_window);

  int32_t line = atoi(argv[0]);
  if (line < 0) {
    uint32_t nlines = buffer_num_lines(v->buffer);
    line = -line;
    line = line >= nlines ? 0 : nlines - line;
  } else if (line > 0) {
    line = line - 1;
  }
  buffer_view_goto(v, (struct location){.line = line, .col = 0});
}

void register_buffer_commands(struct commands *commands) {
  static struct command buffer_commands[] = {
      {.name = "kill-line", .fn = kill_line_cmd},
      {.name = "delete-word", .fn = forward_delete_word_cmd},
      {.name = "backward-delete-word", .fn = backward_delete_word_cmd},
      {.name = "delete-char", .fn = forward_delete_char_cmd},
      {.name = "backward-delete-char", .fn = backward_delete_char_cmd},
      {.name = "backward-char", .fn = backward_char_cmd},
      {.name = "backward-word", .fn = backward_word_cmd},
      {.name = "forward-char", .fn = forward_char_cmd},
      {.name = "forward-word", .fn = forward_word_cmd},
      {.name = "backward-line", .fn = backward_line_cmd},
      {.name = "forward-line", .fn = forward_line_cmd},
      {.name = "end-of-line", .fn = goto_end_of_line_cmd},
      {.name = "beginning-of-line", .fn = goto_beginning_of_line_cmd},
      {.name = "newline", .fn = newline_cmd},
      {.name = "indent", .fn = indent_cmd},
      {.name = "buffer-write-to-file", .fn = to_file_cmd},
      {.name = "set-mark", .fn = set_mark_cmd},
      {.name = "clear-mark", .fn = clear_mark_cmd},
      {.name = "copy", .fn = copy_cmd},
      {.name = "cut", .fn = cut_cmd},
      {.name = "paste", .fn = paste_cmd},
      {.name = "paste-older", .fn = paste_older_cmd},
      {.name = "goto-beginning", .fn = goto_beginning_cmd},
      {.name = "goto-end", .fn = goto_end_cmd},
      {.name = "undo", .fn = undo_cmd},
      {.name = "scroll-down", .fn = scroll_down_cmd},
      {.name = "scroll-up", .fn = scroll_up_cmd},
      {.name = "reload", .fn = reload_cmd},
      {.name = "goto-line", .fn = goto_line},
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

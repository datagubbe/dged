#include <errno.h>
#include <libgen.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "dged/binding.h"

#include "dged/allocator.h"
#include "dged/buffer.h"
#include "dged/buffer_view.h"
#include "dged/buffers.h"
#include "dged/command.h"
#include "dged/display.h"
#include "dged/location.h"
#include "dged/minibuffer.h"
#include "dged/path.h"
#include "dged/s8.h"
#include "dged/settings.h"
#include "dged/text.h"
#include "dged/vec.h"
#include "dired.h"
#if defined(SYNTAX_ENABLE)
#include "dged/syntax.h"
#endif
#include "dged/timers.h"
#include "dged/utf8.h"

#include "bindings.h"
#include "completion.h"
#include "completion/buffer.h"
#include "completion/command.h"
#include "completion/path.h"
#include "dged/window.h"
#include "search-replace.h"

static void (*g_terminate_cb)(void) = NULL;

static int32_t _abort(struct command_ctx ctx, int argc, const char *argv[]) {
  (void)argc;
  (void)argv;

  abort_replace();
  abort_search();
  abort_completion();
  disable_completion(minibuffer_buffer());
  minibuffer_abort_prompt();
  buffer_view_clear_mark(window_buffer_view(ctx.active_window));
  minibuffer_display_timeout(4, "💣 aborted");
  return 0;
}

int32_t unimplemented_command(struct command_ctx ctx, int argc,
                              const char *argv[]) {
  (void)argc;
  (void)argv;
  minibuffer_echo("TODO: %s is not implemented", (const char *)ctx.userdata);
  return 0;
}

static int32_t exit_editor(struct command_ctx ctx, int argc,
                           const char *argv[]) {
  (void)ctx;
  (void)argc;
  (void)argv;

  if (g_terminate_cb != NULL) {
    g_terminate_cb();
  }

  return 0;
}

static void write_file_comp_inserted(void) { minibuffer_execute(); }

static int32_t write_file(struct command_ctx ctx, int argc,
                          const char *argv[]) {
  const char *pth = NULL;
  if (argc == 0) {
    struct completion_provider providers[] = {
        create_path_provider(write_file_comp_inserted)};
    add_completion_providers(minibuffer_buffer(), providers, 1);
    return minibuffer_prompt(ctx, "write to file: ");
  }

  disable_completion(minibuffer_buffer());
  pth = argv[0];
  buffer_set_filename(window_buffer(ctx.active_window), pth);
  buffer_to_file(window_buffer(ctx.active_window));

  return 0;
}

static void run_interactive_comp_inserted(struct command *cmd) {
  (void)cmd;
  minibuffer_execute();
}

int32_t run_interactive(struct command_ctx ctx, int argc, const char *argv[]) {
  if (argc == 0) {
    struct completion_provider providers[] = {
        create_commands_provider(ctx.commands, run_interactive_comp_inserted)};
    add_completion_providers(minibuffer_buffer(), providers, 1);

    return minibuffer_prompt(ctx, "execute: ");
  }

  disable_completion(minibuffer_buffer());
  struct command *cmd = lookup_command(ctx.commands, argv[0]);
  if (cmd != NULL) {
    return execute_command(cmd, ctx.commands, ctx.active_window, ctx.buffers,
                           ctx.display, argc - 1, argv + 1);
  } else {
    minibuffer_echo_timeout(4, "command %s not found", argv[0]);
    return 11;
  }
}

int32_t do_switch_buffer(struct command_ctx ctx, int argc, const char *argv[]) {
  disable_completion(minibuffer_buffer());
  const char *bufname = NULL;
  if (argc == 0) {
    // switch back to prev buffer
    if (window_has_prev_buffer_view(ctx.active_window)) {
      bufname = window_prev_buffer_view(ctx.active_window)->buffer->name;
    } else {
      return 0;
    }
  } else {
    bufname = argv[0];
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

COMMAND_FN("do-switch-buffer", do_switch_buffer, do_switch_buffer, NULL)

static void switch_buffer_comp_inserted(struct buffer *buffer) {
  // TODO: do useful stuff with buffer here
  (void)buffer;
  minibuffer_execute();
}

int32_t switch_buffer(struct command_ctx ctx, int argc, const char *argv[]) {
  if (argc == 0) {
    minibuffer_clear();
    struct completion_provider providers[] = {
        create_buffer_provider(ctx.buffers, switch_buffer_comp_inserted)};
    add_completion_providers(minibuffer_buffer(), providers, 1);

    ctx.self = &do_switch_buffer_command;
    if (window_has_prev_buffer_view(ctx.active_window)) {
      return minibuffer_prompt(
          ctx, "buffer (default %s): ",
          window_prev_buffer_view(ctx.active_window)->buffer->name);
    } else {
      return minibuffer_prompt(ctx, "buffer: ");
    }
  }

  disable_completion(minibuffer_buffer());

  return execute_command(&do_switch_buffer_command, ctx.commands,
                         ctx.active_window, ctx.buffers, ctx.display, argc,
                         argv);
}

int32_t do_kill_buffer(struct command_ctx ctx, int argc, const char *argv[]) {
  disable_completion(minibuffer_buffer());
  const char *bufname = NULL;
  if (argc == 0) {
    // kill current buffer
    bufname = window_buffer(ctx.active_window)->name;
  } else {
    bufname = argv[0];
  }

  if (!buffers_remove(ctx.buffers, bufname)) {
    minibuffer_echo_timeout(4, "buffer %s not found", bufname);
    return 1;
  }

  return 0;
}

COMMAND_FN("do-kill-buffer", do_kill_buffer, do_kill_buffer, NULL)

static void kill_buffer_comp_inserted(struct buffer *buffer) {
  // TODO: do something with buffer
  (void)buffer;
  minibuffer_execute();
}

int32_t kill_buffer(struct command_ctx ctx, int argc, const char *argv[]) {
  if (argc == 0) {
    minibuffer_clear();
    struct completion_provider providers[] = {
        create_buffer_provider(ctx.buffers, kill_buffer_comp_inserted)};
    add_completion_providers(minibuffer_buffer(), providers, 1);

    ctx.self = &do_kill_buffer_command;
    return minibuffer_prompt(ctx, "kill buffer (default %s): ",
                             window_buffer(ctx.active_window)->name);
  }

  disable_completion(minibuffer_buffer());

  return execute_command(&do_switch_buffer_command, ctx.commands,
                         ctx.active_window, ctx.buffers, ctx.display, argc,
                         argv);
}

static struct location draw_timer_value(struct buffer *buffer, double value,
                                        struct location start) {
  struct s8 strval = s8from_fmt("%.2f", value);
  struct location end = buffer_add(buffer, start, strval.s, strval.l);
  s8delete(strval);

  uint32_t color = Color_Green;
  if (value > 5.f) {
    color = Color_Red;
  } else if (value > 2.f) {
    color = Color_Yellow;
  }

  buffer_add_text_property(
      buffer, start,
      (struct location){.col = end.col > 0 ? end.col - 1 : 0, .line = end.line},
      (struct text_property){.type = TextProperty_Colors,
                             .data.colors = (struct text_property_colors){
                                 .set_bg = false,
                                 .set_fg = true,
                                 .fg = color,
                             }});

  return end;
}

static void timers_refresh(struct buffer *buffer, void *userdata) {
  (void)userdata;

  buffer_set_readonly(buffer, false);
  buffer_clear(buffer);
  buffer_begin_bulk_add(buffer);
  timer_vec timers = timers_sorted();
  struct location at = {}, begin = {};
  VEC_FOR_EACH(&timers, const struct timer **ptimer) {
    const struct timer *timer = *ptimer;
    const char *name = timer_name(timer);
    const char *n = name;
    size_t level = 0;
    while (*n) {
      if (*n == '.' && level < 2) {
        ++level;
      }
      ++n;
    }

    size_t namelen = strlen(name);
    struct s8 namefmt = s8from_fmt("%*s", level + namelen, name);
    struct location begin = at;
    begin.col += level;
    at = buffer_add(buffer, at, namefmt.s, namefmt.l);
    s8delete(namefmt);
    buffer_add_text_property(
        buffer, begin, at,
        (struct text_property){.type = TextProperty_Colors,
                               .data.colors = (struct text_property_colors){
                                   .set_bg = false,
                                   .set_fg = true,
                                   .fg = Color_Cyan,
                               }});

    at = buffer_add(buffer, at, (uint8_t *)" - ", 3);
    at = draw_timer_value(buffer, timer_average(timer) / 1e6, at);
    at = buffer_add(buffer, at, (uint8_t *)" ms (min: ", 10);
    at = draw_timer_value(buffer, timer_min(timer) / 1e6, at);
    at = buffer_add(buffer, at, (uint8_t *)", max: ", 7);
    at = draw_timer_value(buffer, timer_max(timer) / 1e6, at);
    at = buffer_add(buffer, at, (uint8_t *)")", 1);
    at = buffer_newline(buffer, at);
  }

  VEC_DESTROY(&timers);
  buffer_end_bulk_add(buffer, region_new(begin, at));
  buffer_set_readonly(buffer, true);
}

int32_t timers(struct command_ctx ctx, int argc, const char *argv[]) {
  (void)argc;
  (void)argv;

  struct buffer *b = buffers_find(ctx.buffers, "*timers*");
  if (b == NULL) {
    struct buffer new_buf = buffer_create("*timers*");
    new_buf.lazy_row_add = false;
    b = buffers_add(ctx.buffers, new_buf);
    buffer_add_update_hook(b, timers_refresh, NULL);
  }

  window_set_buffer(ctx.active_window, b);
  timers_refresh(b, NULL);

  return 0;
}

void buffer_to_list_line(struct buffer *buffer, void *userdata) {
  struct buffer *listbuf = (struct buffer *)userdata;

  struct s8 path = s8new("<no-file>", 9);
  if (buffer->filename != NULL) {
    struct s8 cwd = working_dir();
    s8delete(path);
    path = relative_to(s8(buffer->filename), cwd);
    s8delete(cwd);
  }

  const char *modified =
      buffer->filename != NULL && buffer->modified ? "*" : "";
  char buf[1024];
  size_t written =
      snprintf(buf, 1024, "%-24s %s%s", buffer->name, s8ascstr(path), modified);

  if (written > 0) {
    struct location begin = buffer_end(listbuf);
    buffer_add(listbuf, begin, (uint8_t *)buf, written);
    size_t namelen = strlen(buffer->name);
    uint32_t nchars = utf8_nchars((uint8_t *)buffer->name, namelen);
    buffer_add_text_property(
        listbuf, begin,
        (struct location){.line = begin.line, .col = begin.col + nchars},
        (struct text_property){.type = TextProperty_Colors,
                               .data.colors = (struct text_property_colors){
                                   .set_bg = false,
                                   .set_fg = true,
                                   .fg = Color_Green,
                               }});

    uint32_t nchars_path = utf8_nchars((uint8_t *)path.s, path.l);
    buffer_add_text_property(
        listbuf, (struct location){.line = begin.line, .col = begin.col + 25},
        (struct location){.line = begin.line,
                          .col = begin.col + 25 + nchars_path},
        (struct text_property){.type = TextProperty_Colors,
                               .data.colors = (struct text_property_colors){
                                   .set_bg = false,
                                   .set_fg = true,
                                   .fg = Color_Blue,
                               }});

    buffer_add_text_property(
        listbuf, (struct location){.line = begin.line, .col = 0},
        (struct location){.line = begin.line,
                          .col = buffer_line_length(listbuf, begin.line)},
        (struct text_property){.type = TextProperty_Data,
                               .data.userdata = buffer});
  }

  s8delete(path);
}

int32_t buflist_visit_cmd(struct command_ctx ctx, int argc, const char **argv) {
  (void)argc;
  (void)argv;

  struct window *w = ctx.active_window;

  struct buffer_view *bv = window_buffer_view(w);
  struct text_property *props[16] = {0};
  uint32_t nprops;
  buffer_get_text_properties(bv->buffer, bv->dot, props, 16, &nprops);

  for (uint32_t propi = 0; propi < nprops; ++propi) {
    struct text_property *p = props[propi];
    if (p->type == TextProperty_Data) {
      window_set_buffer(w, p->data.userdata);
      return 0;
    }
  }

  return 0;
}

int32_t buflist_close_cmd(struct command_ctx ctx, int argc,
                          const char *argv[]) {
  return execute_command(&do_switch_buffer_command, ctx.commands,
                         ctx.active_window, ctx.buffers, ctx.display, argc,
                         argv);
}

void buflist_refresh(struct buffer *buffer, void *userdata) {
  struct buffers *buffers = (struct buffers *)userdata;
  buffer_set_readonly(buffer, false);
  buffer_clear(buffer);
  buffer_begin_bulk_add(buffer);
  buffers_for_each(buffers, buffer_to_list_line, buffer);
  buffer_end_bulk_add(buffer, region_new((struct location){.line = 0, .col = 0},
                                         buffer_end(buffer)));
  buffer_set_readonly(buffer, true);
}

int32_t buflist_refresh_cmd(struct command_ctx ctx, int argc,
                            const char *argv[]) {
  (void)argc;
  (void)argv;
  struct buffer *b = buffers_find(ctx.buffers, "*buffers*");
  if (b != NULL) {
    buflist_refresh(b, ctx.buffers);
  }
  return 0;
}

static struct command buflist_refresh_command = {
    .name = "buflist-refresh",
    .fn = buflist_refresh_cmd,
};

int32_t buflist_kill_cmd(struct command_ctx ctx, int argc, const char *argv[]) {
  (void)argc;
  (void)argv;

  struct window *w = ctx.active_window;

  struct buffer_view *bv = window_buffer_view(w);
  struct text_chunk text = buffer_line(bv->buffer, bv->dot.line);

  char *end = (char *)memchr(text.text, ' ', text.nbytes);

  if (end != NULL) {
    uint32_t len = end - (char *)text.text;
    char *bufname = (char *)malloc(len + 1);
    strncpy(bufname, (const char *)text.text, len);
    bufname[len] = '\0';

    buffers_remove(ctx.buffers, bufname);
    free(bufname);
    execute_command(&buflist_refresh_command, ctx.commands, ctx.active_window,
                    ctx.buffers, ctx.display, 0, NULL);
  }

  return 0;
}

int32_t buflist_save_cmd(struct command_ctx ctx, int argc, const char *argv[]) {
  (void)argc;
  (void)argv;

  struct window *w = ctx.active_window;

  struct buffer_view *bv = window_buffer_view(w);
  struct text_chunk text = buffer_line(bv->buffer, bv->dot.line);

  char *end = (char *)memchr(text.text, ' ', text.nbytes);

  if (end != NULL) {
    uint32_t len = end - (char *)text.text;
    char *bufname = (char *)malloc(len + 1);
    strncpy(bufname, (const char *)text.text, len);
    bufname[len] = '\0';

    struct buffer *buffer = buffers_find(ctx.buffers, bufname);
    if (buffer != NULL) {
      buffer_to_file(buffer);
    }
    free(bufname);
    execute_command(&buflist_refresh_command, ctx.commands, ctx.active_window,
                    ctx.buffers, ctx.display, 0, NULL);
  }

  return 0;
}

static void save_buffer(struct buffer *buffer, void *userdata) {
  (void)userdata;
  if (buffer->filename == NULL || !buffer->modified) {
    return;
  }

  buffer_to_file(buffer);
}

int32_t buflist_save_all_cmd(struct command_ctx ctx, int argc,
                             const char *argv[]) {
  (void)argc;
  (void)argv;
  buffers_for_each(ctx.buffers, save_buffer, NULL);
  return 0;
}

int32_t buffer_list(struct command_ctx ctx, int argc, const char *argv[]) {
  (void)argc;
  (void)argv;

  struct buffer *b = buffers_find(ctx.buffers, "*buffers*");
  if (b == NULL) {
    b = buffers_add(ctx.buffers, buffer_create("*buffers*"));
    buffer_add_update_hook(b, buflist_refresh, ctx.buffers);
  }

  struct window *w = ctx.active_window;
  window_set_buffer(ctx.active_window, b);

  buflist_refresh(b, ctx.buffers);

  static struct command buflist_visit = {
      .name = "buflist-visit",
      .fn = buflist_visit_cmd,
  };

  static struct command buflist_kill = {
      .name = "buflist-kill",
      .fn = buflist_kill_cmd,
  };

  static struct command buflist_close = {
      .name = "buflist-close",
      .fn = buflist_close_cmd,
  };

  static struct command buflist_save = {
      .name = "buflist-save",
      .fn = buflist_save_cmd,
  };

  static struct command buflist_save_all = {
      .name = "buflist-save-all",
      .fn = buflist_save_all_cmd,
  };

  struct binding bindings[] = {
      ANONYMOUS_BINDING(ENTER, &buflist_visit),
      ANONYMOUS_BINDING(NUMPAD_ENTER, &buflist_visit),
      ANONYMOUS_BINDING(None, 'k', &buflist_kill),
      ANONYMOUS_BINDING(None, 'q', &buflist_close),
      ANONYMOUS_BINDING(None, 's', &buflist_save),
      ANONYMOUS_BINDING(None, 'S', &buflist_save_all),
      ANONYMOUS_BINDING(None, 'g', &buflist_refresh_command),
  };
  struct keymap km = keymap_create("buflist", 8);
  keymap_bind_keys(&km, bindings, sizeof(bindings) / sizeof(bindings[0]));
  buffer_add_keymap(b, km);
  windows_set_active(w);

  return 0;
}

static void find_file_comp_inserted(void) { minibuffer_execute(); }

static int32_t open_file(struct command_ctx ctx, const char *pth) {

  struct window *active_window = ctx.active_window;
  struct buffers *buffers = ctx.buffers;

  if (active_window == minibuffer_window()) {
    minibuffer_echo_timeout(4, "cannot open files in the minibuffer");
    return 1;
  }

  struct stat sb = {0};
  errno = 0;
  if (stat(pth, &sb) < 0 && errno != ENOENT) {
    minibuffer_echo("stat on %s failed: %s", pth, strerror(errno));
    return 1;
  }

  if (S_ISDIR(sb.st_mode) && errno != ENOENT) {
    struct command *cmd = lookup_command(ctx.commands, "dired");
    if (cmd != NULL) {
      const char *argv[] = {pth};
      return execute_command(cmd, ctx.commands, ctx.active_window, ctx.buffers,
                             ctx.display, 1, argv);
    }

    minibuffer_echo_timeout(4, "dired is not supported");
    return 0;
  }

  struct s8 filename = canonicalize(s8(pth));
  struct buffer *b = buffers_find_by_filename(buffers, s8ascstr(filename));
  s8delete(filename);

  if (b == NULL) {
    b = buffers_add(buffers, buffer_from_file((char *)pth));
  } else {
    buffer_reload(b);
  }

  window_set_buffer(active_window, b);
  minibuffer_echo_timeout(4, "buffer \"%s\" loaded",
                          window_buffer(active_window)->name);

  return 0;
}

int32_t find_file(struct command_ctx ctx, int argc, const char *argv[]) {
  if (argc == 0) {
    minibuffer_clear();
    struct completion_provider providers[] = {
        create_path_provider(find_file_comp_inserted)};
    add_completion_providers(minibuffer_buffer(), providers, 1);

    int32_t r = minibuffer_prompt(ctx, "find file: ");

    // Trigger directly
    complete(minibuffer_buffer(), buffer_end(minibuffer_buffer()));

    return r;
  }

  disable_completion(minibuffer_buffer());

  open_file(ctx, argv[0]);
  return 0;
}

COMMAND_FN("find-file-internal", find_file, find_file, NULL)
int32_t find_file_relative(struct command_ctx ctx, int argc,
                           const char *argv[]) {
  struct buffer *b = window_buffer(ctx.active_window);
  if (b->associated_path == NULL && !buffer_is_backed(b)) {
    minibuffer_echo_timeout(
        4, "buffer %s is not backed by a file and has no associated path",
        b->name);
    return 1;
  }

  const char *path =
      b->associated_path != NULL ? b->associated_path : b->filename;

  struct s8 dir = s8new(path, strlen(path));
  if (!is_dir(dir)) {
    struct s8 file = dir;
    dir = parent(file);
    s8delete(file);
  }

  if (argc == 0) {
    minibuffer_clear();
    struct completion_provider providers[] = {
        create_path_provider(find_file_comp_inserted)};
    add_completion_providers(minibuffer_buffer(), providers, 1);

    ctx.self = &find_file_command;

    struct s8 cwd = working_dir();
    struct s8 reldir = relative_to(dir, cwd);
    s8delete(dir);
    s8delete(cwd);

    if (!s8empty(reldir)) {
      struct s8 dir_with_slash = s8from_fmt("%s/", s8ascstr(reldir));
      minibuffer_prompt_initial(ctx, s8ascstr(dir_with_slash), "find file: ");
      s8delete(dir_with_slash);
    } else {
      minibuffer_prompt(ctx, "find file: ");
    }

    s8delete(reldir);
    complete(minibuffer_buffer(), buffer_end(minibuffer_buffer()));

    return 0;
  }

  // this is called when invoking `find-file-relative` with
  // an argument.
  disable_completion(minibuffer_buffer());
  struct s8 arg = s8(argv[0]);
  struct s8 pth = join_path(dir, arg);
  open_file(ctx, s8ascstr(pth));

  s8delete(pth);
  return 0;
}

#if defined(SYNTAX_ENABLE)
static int32_t syntax_at_point_cmd(struct command_ctx ctx, int argc,
                                   const char *argv[]) {

  (void)argc;
  (void)argv;

  struct buffer_view *view = window_buffer_view(ctx.active_window);
  if (view == NULL || buffer_is_empty(view->buffer)) {
    return 0;
  }

  struct syntax_node node = syntax_node_at(view->buffer, view->dot);
  if (!node.valid) {
    return 0;
  }

  minibuffer_display_timeout(4, "syntax node at (%d, %d): %.*s", view->dot.line,
                             view->dot.col, node.expr.l, node.expr.s);
  syntax_node_free(&node);
  return 0;
}
#endif

static int32_t suspend_cmd(struct command_ctx ctx, int argc,
                           const char *argv[]) {
  (void)argc;
  (void)argv;

  void (*callback)(void) = ctx.userdata;
  callback();
  return 0;
}

void register_global_commands(struct commands *commands,
                              void (*terminate_cb)(void),
                              void (*suspend_cb)(void),
                              struct frame_allocator *alloc) {
  g_terminate_cb = terminate_cb;
  struct command global_commands[] = {
      {.name = "find-file", .fn = find_file},
      {.name = "find-file-relative", .fn = find_file_relative},
      {.name = "write-file", .fn = write_file},
      {.name = "run-command-interactive", .fn = run_interactive},
      {.name = "switch-buffer", .fn = switch_buffer},
      {.name = "kill-buffer", .fn = kill_buffer},
      {.name = "abort", .fn = _abort},
      {.name = "timers", .fn = timers},
      {.name = "buffer-list", .fn = buffer_list},
#if defined(SYNTAX_ENABLE)
      {.name = "syntax-info-at-point", .fn = syntax_at_point_cmd},
#endif
      {.name = "suspend", .fn = suspend_cmd, .userdata = suspend_cb},
      {.name = "exit", .fn = exit_editor}};

  register_commands(commands, global_commands,
                    sizeof(global_commands) / sizeof(global_commands[0]));

  register_search_replace_commands(commands);
  register_dired_commands(commands, alloc);
}

void teardown_global_commands(void) { cleanup_search_replace(); }

#define BUFFER_VIEW_WRAPCMD(fn)                                                \
  static int32_t fn##_cmd(struct command_ctx ctx, int argc,                    \
                          const char *argv[]) {                                \
    (void)argc;                                                                \
    (void)argv;                                                                \
    buffer_view_##fn(window_buffer_view(ctx.active_window));                   \
    return 0;                                                                  \
  }

#define BUFFER_WRAPCMD(fn)                                                     \
  static int32_t fn##_cmd(struct command_ctx ctx, int argc,                    \
                          const char *argv[]) {                                \
    (void)argc;                                                                \
    (void)argv;                                                                \
    buffer_##fn(window_buffer(ctx.active_window));                             \
    return 0;                                                                  \
  }

BUFFER_WRAPCMD(to_file)
BUFFER_WRAPCMD(reload)
BUFFER_VIEW_WRAPCMD(kill_line)
BUFFER_VIEW_WRAPCMD(forward_delete_char)
BUFFER_VIEW_WRAPCMD(backward_delete_char)
BUFFER_VIEW_WRAPCMD(delete_word)
BUFFER_VIEW_WRAPCMD(backward_char)
BUFFER_VIEW_WRAPCMD(backward_word)
BUFFER_VIEW_WRAPCMD(forward_char)
BUFFER_VIEW_WRAPCMD(forward_word)
BUFFER_VIEW_WRAPCMD(backward_line)
BUFFER_VIEW_WRAPCMD(forward_line)
BUFFER_VIEW_WRAPCMD(goto_end_of_line)
BUFFER_VIEW_WRAPCMD(goto_beginning_of_line)
BUFFER_VIEW_WRAPCMD(newline)
BUFFER_VIEW_WRAPCMD(indent)
BUFFER_VIEW_WRAPCMD(indent_alt)
BUFFER_VIEW_WRAPCMD(unindent_line)
BUFFER_VIEW_WRAPCMD(set_mark)
BUFFER_VIEW_WRAPCMD(clear_mark)
BUFFER_VIEW_WRAPCMD(cut)
BUFFER_VIEW_WRAPCMD(paste)
BUFFER_VIEW_WRAPCMD(paste_older)
BUFFER_VIEW_WRAPCMD(goto_beginning)
BUFFER_VIEW_WRAPCMD(goto_end)
BUFFER_VIEW_WRAPCMD(sort_lines)

static int32_t copy_cmd(struct command_ctx ctx, int argc, const char *argv[]) {
  (void)argc;
  (void)argv;
  struct buffer_view *bv = window_buffer_view(ctx.active_window);
  struct region reg = region_new(bv->dot, bv->mark);
  if (bv->mark_set && region_has_size(reg)) {
    /* send the copied text to the display. Display
     * in this case can mean X11/Wayland or terminal.
     * In the case of the terminal it uses OSC52.
     */
    struct text_chunk txt = buffer_region(bv->buffer, reg);

    display_to_clipboard(ctx.display,
                         (struct s8){.s = txt.text, .l = txt.nbytes});
    if (txt.allocated) {
      free(txt.text);
    }
  }

  buffer_view_copy(bv);
  return 0;
}

static int32_t undo_cmd(struct command_ctx ctx, int argc, const char *argv[]) {
  (void)argc;
  (void)argv;
  pause_completion();
  buffer_view_undo(window_buffer_view(ctx.active_window));
  resume_completion();
  return 0;
}

static int32_t scroll_up_cmd(struct command_ctx ctx, int argc,
                             const char *argv[]) {
  (void)argc;
  (void)argv;

  uint32_t height = window_height(ctx.active_window);
  uint32_t amount = height;
  if (height > 3) {
    amount = height - 3;
  }

  struct buffer_view *bv = window_buffer_view(ctx.active_window);
  buffer_view_backward_nlines(bv, amount);
  bv->scroll.line = bv->dot.line;
  return 0;
}

static int32_t scroll_down_cmd(struct command_ctx ctx, int argc,
                               const char *argv[]) {
  (void)argc;
  (void)argv;

  uint32_t height = window_height(ctx.active_window);
  uint32_t amount = height;
  if (height > 3) {
    amount = height - 3;
  }

  struct buffer_view *bv = window_buffer_view(ctx.active_window);
  buffer_view_forward_nlines(bv, amount);
  if (bv->dot.line < buffer_num_lines(bv->buffer)) {
    bv->scroll.line = bv->dot.line;
  }

  return 0;
}

static int32_t goto_line(struct command_ctx ctx, int argc, const char *argv[]) {
  // don't want to goto line in minibuffer
  if (ctx.active_window == minibuffer_window()) {
    return 0;
  }

  if (argc == 0) {
    return minibuffer_prompt(ctx, "line: ");
  }

  struct buffer_view *v = window_buffer_view(ctx.active_window);

  int32_t line = atoi(argv[0]);
  if (line < 0) {
    uint32_t nlines = buffer_num_lines(v->buffer);
    line = -line;
    line = (uint32_t)line >= nlines ? 0 : nlines - line;
  } else if (line > 0) {
    line = line - 1;
  }
  buffer_view_goto(v, (struct location){.line = line, .col = 0});

  return 0;
}

void register_buffer_commands(struct commands *commands) {
  static struct command buffer_commands[] = {
      {.name = "kill-line", .fn = kill_line_cmd},
      {.name = "delete-word", .fn = delete_word_cmd},
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
      {.name = "indent-alt", .fn = indent_alt_cmd},
      {.name = "unindent-line", .fn = unindent_line_cmd},
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
      {.name = "sort-lines", .fn = sort_lines_cmd},
  };

  register_commands(commands, buffer_commands,
                    sizeof(buffer_commands) / sizeof(buffer_commands[0]));
}

static int32_t window_close_cmd(struct command_ctx ctx, int argc,
                                const char *argv[]) {
  (void)argc;
  (void)argv;

  window_close(ctx.active_window);
  return 0;
}

static int32_t window_split_cmd(struct command_ctx ctx, int argc,
                                const char *argv[]) {
  (void)argc;
  (void)argv;

  struct window *resa, *resb;
  window_split(ctx.active_window, &resa, &resb);
  return 0;
}

static int32_t window_hsplit_cmd(struct command_ctx ctx, int argc,
                                 const char *argv[]) {
  (void)argc;
  (void)argv;

  struct window *resa, *resb;
  window_hsplit(ctx.active_window, &resa, &resb);
  return 0;
}

static int32_t window_vsplit_cmd(struct command_ctx ctx, int argc,
                                 const char *argv[]) {
  (void)argc;
  (void)argv;

  struct window *resa, *resb;
  window_vsplit(ctx.active_window, &resa, &resb);
  return 0;
}

static int32_t window_close_others_cmd(struct command_ctx ctx, int argc,
                                       const char *argv[]) {
  (void)argc;
  (void)argv;

  window_close_others(ctx.active_window);
  return 0;
}

static int32_t window_focus_next_cmd(struct command_ctx ctx, int argc,
                                     const char *argv[]) {
  (void)ctx;
  (void)argc;
  (void)argv;

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
  (void)argc;
  (void)argv;

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
      new_value.data.bool_value = strncmp("true", value, 4) == 0 ||
                                  strncmp("yes", value, 3) == 0 ||
                                  strncmp("on", value, 2) == 0;
      break;
    case Setting_Number:
      new_value.data.number_value = atol(value);
      break;
    case Setting_String:
      new_value.data.string_value = (char *)value;
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

#include <string.h>

#include "buffer.h"
#include "buffer_view.h"
#include "display.h"
#include "settings.h"
#include "timers.h"
#include "utf8.h"

HOOK_IMPL(modeline, modeline_hook_cb);

static modeline_hook_vec g_modeline_hooks = {0};
static uint32_t g_modeline_hook_id = 0;

static bool maybe_delete_region(struct buffer_view *view) {
  struct region reg = region_new(view->dot, view->mark);
  if (view->mark_set && region_has_size(reg)) {
    buffer_delete(view->buffer, reg);
    buffer_view_clear_mark(view);
    view->dot = reg.begin;
    return true;
  }

  return false;
}

struct buffer_view buffer_view_create(struct buffer *buffer, bool modeline,
                                      bool line_numbers) {
  struct buffer_view v = (struct buffer_view){
      .dot = (struct location){.line = 0, .col = 0},
      .mark = (struct location){.line = 0, .col = 0},
      .mark_set = false,
      .scroll = (struct location){.line = 0, .col = 0},
      .buffer = buffer,
      .modeline = modeline,
      .line_numbers = line_numbers,
      .fringe_width = 0,
      .needs_render = false,
  };

  return v;
}

struct buffer_view buffer_view_clone(const struct buffer_view *view) {
  struct buffer_view c = {
      .dot = view->dot,
      .mark = view->mark,
      .mark_set = view->mark_set,
      .scroll = view->scroll,
      .buffer = view->buffer,
      .modeline = view->modeline,
      .line_numbers = view->line_numbers,
  };

  return c;
}

void buffer_view_destroy(struct buffer_view *view) { view->buffer = NULL; }

void buffer_view_add(struct buffer_view *view, uint8_t *txt, uint32_t nbytes) {
  maybe_delete_region(view);
  struct location before = view->dot;
  view->dot = buffer_add(view->buffer, view->dot, txt, nbytes);
  if (view->dot.line > before.line) {
    buffer_push_undo_boundary(view->buffer);
  }
}

void buffer_view_goto_beginning(struct buffer_view *view) {
  view->needs_render = true;
  view->dot = (struct location){.line = 0, .col = 0};
}

void buffer_view_goto_end(struct buffer_view *view) {
  view->needs_render = true;
  view->dot = buffer_end(view->buffer);
}

void buffer_view_goto(struct buffer_view *view, struct location to) {
  view->needs_render = true;
  view->dot = buffer_clamp(view->buffer, (int64_t)to.line, (int64_t)to.col);
}

void buffer_view_forward_char(struct buffer_view *view) {
  view->dot = buffer_next_char(view->buffer, view->dot);
}

void buffer_view_backward_char(struct buffer_view *view) {
  view->dot = buffer_previous_char(view->buffer, view->dot);
}

void buffer_view_forward_word(struct buffer_view *view) {
  view->dot = buffer_next_word(view->buffer, view->dot);
}

void buffer_view_backward_word(struct buffer_view *view) {
  struct location before = view->dot;
  view->dot = buffer_previous_word(view->buffer, view->dot);
  if (before.col == 0 && view->dot.col == 0) {
    buffer_view_backward_char(view);
  }
}

void buffer_view_forward_line(struct buffer_view *view) {
  view->dot = buffer_next_line(view->buffer, view->dot);
}

void buffer_view_backward_line(struct buffer_view *view) {
  view->dot = buffer_previous_line(view->buffer, view->dot);
}

void buffer_view_forward_nlines(struct buffer_view *view, uint32_t nlines) {
  view->dot = buffer_clamp(view->buffer, (int64_t)view->dot.line + nlines,
                           (int64_t)view->dot.col);
}

void buffer_view_backward_nlines(struct buffer_view *view, uint32_t nlines) {
  view->dot = buffer_clamp(view->buffer, (int64_t)view->dot.line - nlines,
                           (int64_t)view->dot.col);
}

void buffer_view_goto_end_of_line(struct buffer_view *view) {
  view->dot.col = buffer_line_length(view->buffer, view->dot.line);
}

void buffer_view_goto_beginning_of_line(struct buffer_view *view) {
  view->dot.col = 0;
}

void buffer_view_newline(struct buffer_view *view) {
  view->dot = buffer_newline(view->buffer, view->dot);
  buffer_push_undo_boundary(view->buffer);
}

void buffer_view_indent(struct buffer_view *view) {
  struct region reg = region_new(view->dot, view->mark);
  if (view->mark_set && region_has_size(reg)) {
    for (uint32_t line = reg.begin.line; line <= reg.end.line; ++line) {
      if (buffer_line_length(view->buffer, line) == 0) {
        continue;
      }

      struct location after = buffer_indent(
          view->buffer, (struct location){.line = line, .col = 0});
      view->dot.col += after.col;
    }
  } else {
    view->dot = buffer_indent(view->buffer, view->dot);
  }
}

void buffer_view_indent_alt(struct buffer_view *view) {
  struct region reg = region_new(view->dot, view->mark);
  if (view->mark_set && region_has_size(reg)) {
    for (uint32_t line = reg.begin.line; line <= reg.end.line; ++line) {
      if (buffer_line_length(view->buffer, line) == 0) {
        continue;
      }

      struct location after = buffer_indent_alt(
          view->buffer, (struct location){.line = line, .col = 0});
      view->dot.col += after.col;
    }
  } else {
    view->dot = buffer_indent_alt(view->buffer, view->dot);
  }
}

void buffer_view_copy(struct buffer_view *view) {
  if (!view->mark_set) {
    return;
  }

  view->dot = buffer_copy(view->buffer, region_new(view->dot, view->mark));
  buffer_view_clear_mark(view);
}

void buffer_view_cut(struct buffer_view *view) {
  if (!view->mark_set) {
    return;
  }

  view->dot = buffer_cut(view->buffer, region_new(view->dot, view->mark));
  buffer_view_clear_mark(view);
}

void buffer_view_paste(struct buffer_view *view) {
  maybe_delete_region(view);
  view->dot = buffer_paste(view->buffer, view->dot);
}

void buffer_view_paste_older(struct buffer_view *view) {
  view->dot = buffer_paste_older(view->buffer, view->dot);
}

void buffer_view_forward_delete_char(struct buffer_view *view) {
  buffer_push_undo_boundary(view->buffer);
  if (maybe_delete_region(view)) {
    return;
  }

  view->dot = buffer_delete(
      view->buffer,
      region_new(view->dot, buffer_next_char(view->buffer, view->dot)));
  buffer_push_undo_boundary(view->buffer);
}

void buffer_view_backward_delete_char(struct buffer_view *view) {
  buffer_push_undo_boundary(view->buffer);
  if (maybe_delete_region(view)) {
    return;
  }

  view->dot = buffer_delete(
      view->buffer,
      region_new(buffer_previous_char(view->buffer, view->dot), view->dot));
  buffer_push_undo_boundary(view->buffer);
}

void buffer_view_delete_word(struct buffer_view *view) {
  buffer_push_undo_boundary(view->buffer);
  if (maybe_delete_region(view)) {
    return;
  }

  struct region word = buffer_word_at(view->buffer, view->dot);

  if (region_has_size(word)) {
    buffer_delete(view->buffer, word);
    view->dot = word.begin;
  } else {
    // fall back to being a normal delete to keep
    // progressing
    view->dot = buffer_delete(
        view->buffer,
        region_new(view->dot, buffer_next_char(view->buffer, view->dot)));
  }
  buffer_push_undo_boundary(view->buffer);
}

void buffer_view_kill_line(struct buffer_view *view) {
  buffer_push_undo_boundary(view->buffer);
  uint32_t ncols =
      buffer_line_length(view->buffer, view->dot.line) - view->dot.col;

  uint32_t line = view->dot.line;
  uint32_t col = view->dot.col + ncols;

  // kill the newline if we are at the end of the line
  if (ncols == 0) {
    struct location loc = buffer_next_char(view->buffer, view->dot);
    line = loc.line;
    col = loc.col;
  }

  struct region reg = region_new(view->dot, (struct location){
                                                .line = line,
                                                .col = col,
                                            });

  buffer_cut(view->buffer, reg);
  buffer_push_undo_boundary(view->buffer);
}

void buffer_view_sort_lines(struct buffer_view *view) {
  struct region reg = region_new(view->dot, view->mark);
  if (view->mark_set && region_has_size(reg)) {
    if (reg.end.line > 0 &&
        buffer_line_length(view->buffer, reg.end.line) == 0) {
      reg.end.line -= 1;
    }

    buffer_sort_lines(view->buffer, reg.begin.line, reg.end.line);
    buffer_view_clear_mark(view);
  }
}

void buffer_view_set_mark(struct buffer_view *view) {
  buffer_view_set_mark_at(view, view->dot);
}

void buffer_view_clear_mark(struct buffer_view *view) {
  view->mark_set = false;
}

void buffer_view_set_mark_at(struct buffer_view *view, struct location mark) {
  view->mark = mark;
  view->mark_set = true;
}

struct location buffer_view_dot_to_relative(struct buffer_view *view) {
  return (struct location){
      .line = view->dot.line - view->scroll.line,
      .col = view->dot.col - view->scroll.col + view->fringe_width,
  };
}

struct location buffer_view_dot_to_visual(struct buffer_view *view) {
  return buffer_view_dot_to_relative(view);
}

void buffer_view_undo(struct buffer_view *view) {
  view->dot = buffer_undo(view->buffer, view->dot);
}

static uint32_t longest_linenum(struct buffer_view *view) {
  uint32_t total_lines = buffer_num_lines(view->buffer);
  uint32_t longest_nchars = 10;
  if (total_lines < 10) {
    longest_nchars = 1;
  } else if (total_lines < 100) {
    longest_nchars = 2;
  } else if (total_lines < 1000) {
    longest_nchars = 3;
  } else if (total_lines < 10000) {
    longest_nchars = 4;
  } else if (total_lines < 100000) {
    longest_nchars = 5;
  } else if (total_lines < 1000000) {
    longest_nchars = 6;
  } else if (total_lines < 10000000) {
    longest_nchars = 7;
  } else if (total_lines < 100000000) {
    longest_nchars = 8;
  } else if (total_lines < 1000000000) {
    longest_nchars = 9;
  }

  return longest_nchars;
}

static uint32_t render_line_numbers(struct buffer_view *view,
                                    struct command_list *commands,
                                    uint32_t height) {
  uint32_t longest_nchars = longest_linenum(view);
  static char buf[16];

  uint32_t nlines_buf = buffer_num_lines(view->buffer);
  uint32_t line = view->scroll.line;
  uint32_t relline = 0;

  for (; relline < height && line < nlines_buf; ++line, ++relline) {
    command_list_set_index_color_bg(commands, Color_BrightBlack);
    command_list_set_index_color_fg(
        commands, line == view->dot.line ? Color_BrightWhite : Color_White);
    uint32_t chars = snprintf(buf, 16, "%*d", longest_nchars + 1, line + 1);
    command_list_draw_text_copy(commands, 0, relline, (uint8_t *)buf, chars);
    command_list_reset_color(commands);
    command_list_draw_repeated(commands, longest_nchars + 1, relline, ' ', 1);
  }

  for (; relline < height; ++relline) {
    command_list_set_index_color_bg(commands, Color_BrightBlack);
    command_list_set_index_color_fg(commands, Color_White);
    command_list_draw_repeated(commands, 0, relline, ' ', longest_nchars + 1);
    command_list_reset_color(commands);
    command_list_draw_repeated(commands, longest_nchars + 1, relline, ' ', 1);
  }

  return longest_nchars + 2;
}

static void render_modeline(struct buffer_view *view,
                            struct command_list *commands, uint32_t window_id,
                            uint32_t width, uint32_t height, float frame_time) {
  time_t now = time(NULL);
  struct tm *lt = localtime(&now);

  char left[1024] = {};
  char right[1024] = {};

  size_t left_len = snprintf(left, 1024, "  %c%c %d:%-16s (%d, %d) (%s) ",
                             view->buffer->modified ? '*' : '-',
                             view->buffer->readonly ? '%' : '-', window_id,
                             view->buffer->name, view->dot.line + 1,
                             view->dot.col, view->buffer->lang.name);

  /* insert hook content on the left */
  VEC_FOR_EACH(&g_modeline_hooks, struct modeline_hook * hook) {
    struct s8 content = hook->callback(view, hook->userdata);
    if (content.l > 0) {
      left_len += snprintf(left + left_len, 1024 - left_len, "[%.*s] ",
                           content.l, content.s);
      s8delete(content);
    }
  }

  size_t right_len = snprintf(right, 1024, " (%.2f ms) %02d:%02d",
                              frame_time / 1e6, lt->tm_hour, lt->tm_min);

  /* clamp all the widths with priority:
   * 1. left
   * 2. right
   * 3. mid
   */
  left_len = left_len > width ? width : left_len;
  right_len = left_len + right_len > width ? width - left_len : right_len;
  size_t mid_len =
      left_len + right_len < width ? width - left_len - right_len : 0;

  char mid[mid_len + 1];
  if (mid_len > 0) {
    memset(mid, '-', mid_len);
    mid[0] = ' ';
    mid[mid_len - 1] = ' ';
  }
  mid[mid_len] = '\0';

  if (left_len > 0) {
    command_list_set_index_color_bg(commands, Color_BrightBlack);
    command_list_set_index_color_fg(commands, Color_White);
    command_list_draw_text_copy(commands, 0, height - 1, (uint8_t *)left,
                                left_len);
  }

  if (mid_len > 0) {
    command_list_set_index_color_bg(commands, Color_BrightBlack);
    command_list_set_index_color_fg(commands, Color_White);
    command_list_draw_text_copy(commands, left_len, height - 1, (uint8_t *)mid,
                                mid_len);
  }

  if (right_len > 0) {
    command_list_set_index_color_bg(commands, Color_BrightBlack);
    command_list_set_index_color_fg(commands, Color_White);
    command_list_draw_text_copy(commands, left_len + mid_len, height - 1,
                                (uint8_t *)right, right_len);
  }

  command_list_reset_color(commands);
}

bool buffer_view_update(struct buffer_view *view,
                        struct buffer_view_update_params *params) {

  bool needs_render = view->needs_render;
  struct timer *buffer_update_timer =
      timer_start("update-windows.buffer-update");
  buffer_update(view->buffer);
  timer_stop(buffer_update_timer);

  needs_render |= view->buffer->needs_render;

  uint32_t height = params->height;
  uint32_t width = params->width;

  /* Make sure the dot is always inside buffer limits.
   * It can be outside for example if the text is changed elsewhere. */
  view->dot = buffer_clamp(view->buffer, (int64_t)view->dot.line,
                           (int64_t)view->dot.col);

  // render modeline
  struct timer *render_modeline_timer =
      timer_start("update-windows.modeline-render");
  uint32_t modeline_height = 0;
  if (view->modeline) {
    modeline_height = 1;
    render_modeline(view, params->commands, params->window_id, params->width,
                    params->height, params->frame_time);
  }

  height -= modeline_height;

  // update scroll position if needed
  if (view->dot.line >= view->scroll.line + height ||
      view->dot.line < view->scroll.line) {
    // put dot in the middle, height-wise
    view->scroll.line =
        buffer_clamp(view->buffer, (int64_t)view->dot.line - params->height / 2,
                     0)
            .line;
  }
  timer_stop(render_modeline_timer);

  // render line numbers
  struct timer *render_linenumbers_timer =
      timer_start("update-windows.linenum-render");
  uint32_t linum_width = 0;
  if (view->line_numbers) {
    linum_width = render_line_numbers(view, params->commands, height);
  }

  width -= linum_width;
  view->fringe_width = linum_width;

  if (view->dot.col >= view->scroll.col + width ||
      view->dot.col < view->scroll.col) {
    view->scroll.col = buffer_clamp(view->buffer, view->dot.line,
                                    (int64_t)view->dot.col - params->width / 2)
                           .col;
  }
  timer_stop(render_linenumbers_timer);

  // color region
  if (view->mark_set) {
    struct region reg = region_new(view->dot, view->mark);
    if (region_has_size(reg)) {
      buffer_add_text_property(view->buffer, reg.begin, reg.end,
                               (struct text_property){
                                   .type = TextProperty_Colors,
                                   .data.colors =
                                       (struct text_property_colors){
                                           .set_bg = true,
                                           .bg = 5,
                                           .set_fg = false,
                                       },
                               });
    }
  }

  // render buffer
  struct timer *render_buffer_timer =
      timer_start("update-windows.buffer-render");

  struct setting *tw = lang_setting(&view->buffer->lang, "tab-width");
  if (tw == NULL) {
    tw = settings_get("editor.tab-width");
  }

  uint32_t tab_width = 4;
  if (tw != NULL && tw->value.type == Setting_Number) {
    tab_width = tw->value.data.number_value;
  }

  struct command_list *buf_cmds = command_list_create(
      width * height, params->frame_alloc, params->window_x + linum_width,
      params->window_y, tab_width, view->buffer->name);
  struct buffer_render_params render_params = {
      .commands = buf_cmds,
      .origin = view->scroll,
      .width = width,
      .height = height,
  };
  buffer_render(view->buffer, &render_params);

  // draw buffer commands nested inside this command list
  command_list_draw_command_list(params->commands, buf_cmds);
  timer_stop(render_buffer_timer);

  view->needs_render = false;
  return needs_render;
}

uint32_t buffer_view_add_modeline_hook(modeline_hook_cb callback,
                                       void *userdata) {
  if (VEC_CAPACITY(&g_modeline_hooks) == 0) {
    VEC_INIT(&g_modeline_hooks, 8);
  }

  return insert_modeline_hook(&g_modeline_hooks, &g_modeline_hook_id, callback,
                              userdata);
}

void buffer_view_remove_modeline_hook(uint32_t hook_id,
                                      remove_hook_cb callback) {
  remove_modeline_hook(&g_modeline_hooks, hook_id, callback);
}

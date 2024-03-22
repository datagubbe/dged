#include <string.h>

#include "buffer.h"
#include "buffer_view.h"
#include "display.h"
#include "timers.h"
#include "utf8.h"

struct modeline {
  uint8_t *buffer;
  uint32_t sz;
};

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
      .modeline = NULL,
      .line_numbers = line_numbers,
      .fringe_width = 0,
  };

  if (modeline) {
    v.modeline = calloc(1, sizeof(struct modeline));
    v.modeline->buffer = malloc(1024);
    v.modeline->sz = 1024;
    v.modeline->buffer[0] = '\0';
  }

  return v;
}

struct buffer_view buffer_view_clone(const struct buffer_view *view) {
  struct buffer_view c = {
      .dot = view->dot,
      .mark = view->mark,
      .mark_set = view->mark_set,
      .scroll = view->scroll,
      .buffer = view->buffer,
      .modeline = NULL,
      .line_numbers = view->line_numbers,
  };

  if (view->modeline) {
    c.modeline = calloc(1, sizeof(struct modeline));
    c.modeline->buffer = malloc(view->modeline->sz);
    memcpy(c.modeline->buffer, view->modeline->buffer, view->modeline->sz);
  }

  return c;
}

void buffer_view_destroy(struct buffer_view *view) {
  if (view->modeline != NULL) {
    free(view->modeline->buffer);
    free(view->modeline);
  }
}

void buffer_view_add(struct buffer_view *view, uint8_t *txt, uint32_t nbytes) {
  maybe_delete_region(view);
  view->dot = buffer_add(view->buffer, view->dot, txt, nbytes);
}

void buffer_view_goto_beginning(struct buffer_view *view) {
  view->dot = (struct location){.line = 0, .col = 0};
}

void buffer_view_goto_end(struct buffer_view *view) {
  view->dot = buffer_end(view->buffer);
}

void buffer_view_goto(struct buffer_view *view, struct location to) {
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
  view->dot = buffer_previous_word(view->buffer, view->dot);
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
  view->dot.col = buffer_num_chars(view->buffer, view->dot.line);
}

void buffer_view_goto_beginning_of_line(struct buffer_view *view) {
  view->dot.col = 0;
}

void buffer_view_newline(struct buffer_view *view) {
  view->dot = buffer_newline(view->buffer, view->dot);
}

void buffer_view_indent(struct buffer_view *view) {
  view->dot = buffer_indent(view->buffer, view->dot);
}

void buffer_view_indent_alt(struct buffer_view *view) {
  view->dot = buffer_indent_alt(view->buffer, view->dot);
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
  if (maybe_delete_region(view)) {
    return;
  }

  view->dot = buffer_delete(
      view->buffer,
      region_new(view->dot, buffer_next_char(view->buffer, view->dot)));
}

void buffer_view_backward_delete_char(struct buffer_view *view) {
  if (maybe_delete_region(view)) {
    return;
  }

  view->dot = buffer_delete(
      view->buffer,
      region_new(buffer_previous_char(view->buffer, view->dot), view->dot));
}

void buffer_view_delete_word(struct buffer_view *view) {
  if (maybe_delete_region(view)) {
    return;
  }

  struct region word = buffer_word_at(view->buffer, view->dot);

  if (region_has_size(word)) {
    buffer_delete(view->buffer, word);
    view->dot = word.begin;
  }
}

void buffer_view_kill_line(struct buffer_view *view) {
  uint32_t nchars =
      buffer_num_chars(view->buffer, view->dot.line) - view->dot.col;
  if (nchars == 0) {
    nchars = 1;
  }

  struct region reg = region_new(view->dot, (struct location){
                                                .line = view->dot.line,
                                                .col = view->dot.col + nchars,
                                            });

  buffer_cut(view->buffer, reg);
}

void buffer_view_sort_lines(struct buffer_view *view) {
  struct region reg = region_new(view->dot, view->mark);
  if (view->mark_set && region_has_size(reg)) {
    if (reg.end.line > 0 && buffer_num_chars(view->buffer, reg.end.line) == 0) {
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
  // calculate visual column index for dot column
  struct text_chunk c = buffer_line(view->buffer, view->dot.line);
  uint32_t width = visual_string_width(c.text, c.nbytes, 0, view->dot.col);
  if (view->scroll.col > 0) {
    width -= visual_string_width(c.text, c.nbytes, 0, view->scroll.col);
  }

  struct location l = buffer_view_dot_to_relative(view);
  l.col = width + view->fringe_width;

  if (c.allocated) {
    free(c.text);
  }

  return l;
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
    command_list_set_index_color_bg(commands, 8);
    command_list_set_index_color_fg(commands, line == view->dot.line ? 15 : 7);
    uint32_t chars = snprintf(buf, 16, "%*d", longest_nchars + 1, line + 1);
    command_list_draw_text_copy(commands, 0, relline, (uint8_t *)buf, chars);
    command_list_reset_color(commands);
    command_list_draw_repeated(commands, longest_nchars + 1, relline, ' ', 1);
  }

  for (; relline < height; ++relline) {
    command_list_set_index_color_bg(commands, 8);
    command_list_set_index_color_fg(commands, 7);
    command_list_draw_repeated(commands, 0, relline, ' ', longest_nchars + 1);
    command_list_reset_color(commands);
    command_list_draw_repeated(commands, longest_nchars + 1, relline, ' ', 1);
  }

  return longest_nchars + 2;
}

static void render_modeline(struct modeline *modeline, struct buffer_view *view,
                            struct command_list *commands, uint32_t window_id,
                            uint32_t width, uint32_t height, float frame_time) {
  char buf[width * 4];
  memset(buf, 0, width * 4);

  time_t now = time(NULL);
  struct tm *lt = localtime(&now);
  static char left[128] = {0};
  static char right[128] = {0};

  snprintf(left, 128, "  %c%c %d:%-16s (%d, %d) (%s)",
           view->buffer->modified ? '*' : '-',
           view->buffer->readonly ? '%' : '-', window_id, view->buffer->name,
           view->dot.line + 1, view->dot.col, view->buffer->lang.name);
  snprintf(right, 128, "(%.2f ms) %02d:%02d", frame_time / 1e6, lt->tm_hour,
           lt->tm_min);

  snprintf(buf, width * 4, "%s%*s%s", left,
           (int)(width - (strlen(left) + strlen(right))), "", right);

  if (strcmp(buf, (char *)modeline->buffer) != 0) {
    modeline->buffer = realloc(modeline->buffer, width * 4);
    modeline->sz = width * 4;

    uint32_t len = strlen(buf);
    len = (len + 1) > modeline->sz ? modeline->sz - 1 : len;
    memcpy(modeline->buffer, buf, len);
    modeline->buffer[len] = '\0';
  }

  command_list_set_index_color_bg(commands, 8);
  command_list_draw_text(commands, 0, height - 1, modeline->buffer,
                         strlen((char *)modeline->buffer));
  command_list_reset_color(commands);
}

void buffer_view_update(struct buffer_view *view,
                        struct buffer_view_update_params *params) {

  struct timer *buffer_update_timer =
      timer_start("update-windows.buffer-update");
  struct buffer_update_params update_params = {};
  buffer_update(view->buffer, &update_params);
  timer_stop(buffer_update_timer);

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
  if (view->modeline != NULL) {
    modeline_height = 1;
    render_modeline(view->modeline, view, params->commands, params->window_id,
                    params->width, params->height, params->frame_time);
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
    view->scroll.col =
        buffer_clamp(view->buffer, view->dot.line, view->dot.col).col;
  }
  timer_stop(render_linenumbers_timer);

  // color region
  if (view->mark_set) {
    struct region reg = region_new(view->dot, view->mark);
    if (region_has_size(reg)) {
      buffer_add_text_property(view->buffer, reg.begin, reg.end,
                               (struct text_property){
                                   .type = TextProperty_Colors,
                                   .colors =
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
  struct command_list *buf_cmds = command_list_create(
      width * height, params->frame_alloc, params->window_x + linum_width,
      params->window_y, view->buffer->name);
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

  // TODO: move to somewhere where more correct if buffers
  // are in more than one view (same with buffer hooks).
  buffer_clear_text_properties(view->buffer);
}

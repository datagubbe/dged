#ifndef _BUFFER_VIEW_H
#define _BUFFER_VIEW_H

#include <stddef.h>

#include "location.h"

struct buffer;

/**
 * A view of a buffer.
 *
 * This contains the mark and dot as well as the scroll position for a buffer.
 */
struct buffer_view {
  /** Location of dot (cursor) */
  struct location dot;

  /** Location of mark (where a selection starts) */
  struct location mark;

  /** Current buffer scroll position */
  struct location scroll;

  /** Pointer to the actual buffer */
  struct buffer *buffer;

  /** Modeline buffer (may be NULL) */
  struct modeline *modeline;

  /** Current left fringe size */
  uint32_t fringe_width;

  /** Does the buffer show line numbers */
  bool line_numbers;

  /** True if the start of a selection has been set */
  bool mark_set;
};

struct buffer_view buffer_view_create(struct buffer *buffer, bool modeline,
                                      bool line_numbers);
struct buffer_view buffer_view_clone(const struct buffer_view *view);
void buffer_view_destroy(struct buffer_view *view);

void buffer_view_add(struct buffer_view *view, uint8_t *txt, uint32_t nbytes);

void buffer_view_goto_beginning(struct buffer_view *view);
void buffer_view_goto_end(struct buffer_view *view);
void buffer_view_goto(struct buffer_view *view, struct location to);
void buffer_view_goto_end_of_line(struct buffer_view *view);
void buffer_view_goto_beginning_of_line(struct buffer_view *view);

void buffer_view_forward_char(struct buffer_view *view);
void buffer_view_backward_char(struct buffer_view *view);
void buffer_view_forward_word(struct buffer_view *view);
void buffer_view_backward_word(struct buffer_view *view);
void buffer_view_forward_line(struct buffer_view *view);
void buffer_view_backward_line(struct buffer_view *view);
void buffer_view_forward_nlines(struct buffer_view *view, uint32_t nlines);
void buffer_view_backward_nlines(struct buffer_view *view, uint32_t nlines);

void buffer_view_forward_delete_char(struct buffer_view *view);
void buffer_view_backward_delete_char(struct buffer_view *view);
void buffer_view_delete_word(struct buffer_view *view);

void buffer_view_kill_line(struct buffer_view *view);

void buffer_view_newline(struct buffer_view *view);
void buffer_view_indent(struct buffer_view *view);

void buffer_view_copy(struct buffer_view *view);
void buffer_view_cut(struct buffer_view *view);
void buffer_view_paste(struct buffer_view *view);
void buffer_view_paste_older(struct buffer_view *view);

void buffer_view_set_mark(struct buffer_view *view);
void buffer_view_clear_mark(struct buffer_view *view);
void buffer_view_set_mark_at(struct buffer_view *view, struct location mark);

struct location buffer_view_dot_to_relative(struct buffer_view *view);
struct location buffer_view_dot_to_visual(struct buffer_view *view);

void buffer_view_undo(struct buffer_view *view);

struct buffer_view_update_params {
  struct command_list *commands;
  void *(*frame_alloc)(size_t);
  uint32_t window_id;
  int64_t frame_time;
  uint32_t width;
  uint32_t height;
  uint32_t window_x;
  uint32_t window_y;
};

void buffer_view_update(struct buffer_view *view,
                        struct buffer_view_update_params *params);

#endif

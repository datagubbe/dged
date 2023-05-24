#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "bits/stdint-uintn.h"
#include "command.h"
#include "lang.h"
#include "text.h"
#include "undo.h"
#include "window.h"

struct keymap;
struct command_list;

/**
 * Margins where buffer text should not be
 */
struct margin {
  uint32_t left;
  uint32_t right;
  uint32_t top;
  uint32_t bottom;
};

/** Callback for line rendering hooks */
typedef void (*line_render_cb)(struct text_chunk *line_data, uint32_t line,
                               struct command_list *commands, void *userdata);

typedef void (*line_render_empty_cb)(uint32_t line,
                                     struct command_list *commands,
                                     void *userdata);

/**
 * A line render hook
 *
 * A callback paired with userdata
 */
struct line_render_hook {
  line_render_cb callback;
  line_render_empty_cb empty_callback;
  void *userdata;
};

/**
 * Result of updating a buffer hook
 */
struct update_hook_result {
  /** Desired margins for this hook */
  struct margin margins;

  /** Hook to be added to rendering of buffer lines */
  struct line_render_hook line_render_hook;
};

/** Buffer update hook callback function */
typedef struct update_hook_result (*update_hook_cb)(
    struct buffer_view *view, struct command_list *commands, uint32_t width,
    uint32_t height, uint64_t frame_time, void *userdata);

/**
 * A buffer update hook.
 *
 * Can be used to implement custom behavior on top of a buffer. Used for
 * minibuffer, line numbers, modeline etc.
 */
struct update_hook {
  /** Callback function */
  update_hook_cb callback;

  /** Optional userdata to pass to the callback function unmodified */
  void *userdata;
};

typedef void (*create_hook_cb)(struct buffer *buffer, void *userdata);

/**
 * A set of update hooks
 */
struct update_hooks {
  /** The update hooks */
  struct update_hook hooks[32];

  /** The number of update hooks */
  uint32_t nhooks;
};

struct buffer_location {
  uint32_t line;
  uint32_t col;
};

struct match {
  struct buffer_location begin;
  struct buffer_location end;
};

struct buffer_view {
  /** Location of dot (cursor) */
  struct buffer_location dot;

  /** Location of mark (where a selection starts) */
  struct buffer_location mark;

  /** Current buffer scroll position */
  struct buffer_location scroll;

  /** True if the start of a selection has been set */
  bool mark_set;

  /** Modeline buffer (may be NULL) */
  struct modeline *modeline;

  bool line_numbers;

  struct buffer *buffer;
};

struct buffer_view buffer_view_create(struct buffer *buffer, bool modeline,
                                      bool line_numbers);
struct buffer_view buffer_view_clone(struct buffer_view *view);

void buffer_view_scroll_down(struct buffer_view *view, uint32_t height);
void buffer_view_scroll_up(struct buffer_view *view, uint32_t height);

void buffer_view_destroy(struct buffer_view *view);

/**
 * A buffer of text that can be modified, read from and written to disk.
 *
 * This is the central data structure of dged and most other behavior is
 * implemented on top of it.
 */
struct buffer {

  /** Buffer name */
  char *name;

  /** Associated filename, this is where the buffer will be saved to */
  char *filename;

  struct timespec last_write;

  /** Text data structure */
  struct text *text;

  /** Buffer update hooks */
  struct update_hooks update_hooks;

  /** Buffer undo stack */
  struct undo_stack undo;

  /** Has this buffer been modified from when it was last saved */
  bool modified;

  /** Can this buffer be changed */
  bool readonly;

  /** Buffer programming language */
  struct language lang;
};

struct buffer buffer_create(char *name);
void buffer_destroy(struct buffer *buffer);

void buffer_static_init();
void buffer_static_teardown();

int buffer_add_text(struct buffer_view *view, uint8_t *text, uint32_t nbytes);
void buffer_set_text(struct buffer *buffer, uint8_t *text, uint32_t nbytes);
void buffer_clear(struct buffer_view *view);
bool buffer_is_empty(struct buffer *buffer);
bool buffer_is_modified(struct buffer *buffer);
bool buffer_is_readonly(struct buffer *buffer);
void buffer_set_readonly(struct buffer *buffer, bool readonly);
bool buffer_is_backed(struct buffer *buffer);

void buffer_kill_line(struct buffer_view *view);
void buffer_forward_delete_char(struct buffer_view *view);
void buffer_forward_delete_word(struct buffer_view *view);
void buffer_backward_delete_char(struct buffer_view *view);
void buffer_backward_delete_word(struct buffer_view *view);
void buffer_backward_char(struct buffer_view *view);
void buffer_backward_word(struct buffer_view *view);
void buffer_forward_char(struct buffer_view *view);
void buffer_forward_word(struct buffer_view *view);
void buffer_backward_line(struct buffer_view *view);
void buffer_forward_line(struct buffer_view *view);
void buffer_end_of_line(struct buffer_view *view);
void buffer_beginning_of_line(struct buffer_view *view);
void buffer_newline(struct buffer_view *view);
void buffer_indent(struct buffer_view *view);

void buffer_undo(struct buffer_view *view);

void buffer_goto_beginning(struct buffer_view *view);
void buffer_goto_end(struct buffer_view *view);
void buffer_goto(struct buffer_view *view, uint32_t line, uint32_t col);

void buffer_find(struct buffer *buffer, const char *pattern,
                 struct match **matches, uint32_t *nmatches);

void buffer_set_mark(struct buffer_view *view);
void buffer_clear_mark(struct buffer_view *view);
void buffer_set_mark_at(struct buffer_view *view, uint32_t line, uint32_t col);

void buffer_copy(struct buffer_view *view);
void buffer_paste(struct buffer_view *view);
void buffer_paste_older(struct buffer_view *view);
void buffer_cut(struct buffer_view *view);

struct text_chunk buffer_get_line(struct buffer *buffer, uint32_t line);

uint32_t buffer_add_update_hook(struct buffer *buffer, update_hook_cb hook,
                                void *userdata);

uint32_t buffer_add_create_hook(create_hook_cb hook, void *userdata);

struct buffer buffer_from_file(char *filename);
void buffer_to_file(struct buffer *buffer);
void buffer_write_to(struct buffer *buffer, const char *filename);
void buffer_reload(struct buffer *buffer);

void buffer_update(struct buffer_view *view, uint32_t window_id, uint32_t width,
                   uint32_t height, struct command_list *commands,
                   uint64_t frame_time, uint32_t *relline, uint32_t *relcol);

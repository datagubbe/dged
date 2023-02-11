#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "command.h"
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
    struct buffer *buffer, struct command_list *commands, uint32_t width,
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

  /** Text data structure */
  struct text *text;

  /** Location of dot (cursor) */
  struct buffer_location dot;

  /** Location of mark (where a selection starts) */
  struct buffer_location mark;

  /** True if the start of a selection has been set */
  bool mark_set;

  /** Buffer-local keymaps in reverse priority order */
  struct keymap *keymaps;

  /** Number of buffer-local keymaps */
  uint32_t nkeymaps;

  /** Maximum number of keymaps */
  uint32_t nkeymaps_max;

  /** Current buffer scroll position */
  struct buffer_location scroll;

  /** Buffer update hooks */
  struct update_hooks update_hooks;

  /** Buffer undo stack */
  struct undo_stack undo;

  /** Has this buffer been modified from when it was last saved */
  bool modified;

  /** Can this buffer be changed */
  bool readonly;

  /** Modeline buffer (may be NULL) */
  struct modeline *modeline;
};

struct buffer buffer_create(char *name, bool modeline);
void buffer_destroy(struct buffer *buffer);
void buffer_static_teardown();

uint32_t buffer_keymaps(struct buffer *buffer, struct keymap **keymaps_out);
void buffer_add_keymap(struct buffer *buffer, struct keymap *keymap);

int buffer_add_text(struct buffer *buffer, uint8_t *text, uint32_t nbytes);
void buffer_clear(struct buffer *buffer);
bool buffer_is_empty(struct buffer *buffer);
bool buffer_is_modified(struct buffer *buffer);
bool buffer_is_readonly(struct buffer *buffer);
void buffer_set_readonly(struct buffer *buffer, bool readonly);

void buffer_kill_line(struct buffer *buffer);
void buffer_forward_delete_char(struct buffer *buffer);
void buffer_backward_delete_char(struct buffer *buffer);
void buffer_backward_char(struct buffer *buffer);
void buffer_backward_word(struct buffer *buffer);
void buffer_forward_char(struct buffer *buffer);
void buffer_forward_word(struct buffer *buffer);
void buffer_backward_line(struct buffer *buffer);
void buffer_forward_line(struct buffer *buffer);
void buffer_end_of_line(struct buffer *buffer);
void buffer_beginning_of_line(struct buffer *buffer);
void buffer_newline(struct buffer *buffer);
void buffer_indent(struct buffer *buffer);

void buffer_undo(struct buffer *buffer);

void buffer_goto_beginning(struct buffer *buffer);
void buffer_goto_end(struct buffer *buffer);
void buffer_goto(struct buffer *buffer, uint32_t line, uint32_t col);

void buffer_set_mark(struct buffer *buffer);
void buffer_clear_mark(struct buffer *buffer);
void buffer_set_mark_at(struct buffer *buffer, uint32_t line, uint32_t col);

void buffer_copy(struct buffer *buffer);
void buffer_paste(struct buffer *buffer);
void buffer_paste_older(struct buffer *buffer);
void buffer_cut(struct buffer *buffer);

struct text_chunk buffer_get_line(struct buffer *buffer, uint32_t line);

uint32_t buffer_add_update_hook(struct buffer *buffer, update_hook_cb hook,
                                void *userdata);

struct buffer buffer_from_file(char *filename);
void buffer_to_file(struct buffer *buffer);
void buffer_write_to(struct buffer *buffer, const char *filename);

void buffer_update(struct buffer *buffer, uint32_t width, uint32_t height,
                   struct command_list *commands, uint64_t frame_time,
                   uint32_t *relline, uint32_t *relcol);

// commands
#define BUFFER_WRAPCMD(fn)                                                     \
  static int32_t fn##_cmd(struct command_ctx ctx, int argc,                    \
                          const char *argv[]) {                                \
    fn(ctx.active_window->buffer);                                             \
    return 0;                                                                  \
  }

BUFFER_WRAPCMD(buffer_kill_line);
BUFFER_WRAPCMD(buffer_forward_delete_char);
BUFFER_WRAPCMD(buffer_backward_delete_char);
BUFFER_WRAPCMD(buffer_backward_char);
BUFFER_WRAPCMD(buffer_backward_word);
BUFFER_WRAPCMD(buffer_forward_char);
BUFFER_WRAPCMD(buffer_forward_word);
BUFFER_WRAPCMD(buffer_backward_line);
BUFFER_WRAPCMD(buffer_forward_line);
BUFFER_WRAPCMD(buffer_end_of_line);
BUFFER_WRAPCMD(buffer_beginning_of_line);
BUFFER_WRAPCMD(buffer_newline);
BUFFER_WRAPCMD(buffer_indent);
BUFFER_WRAPCMD(buffer_to_file);
BUFFER_WRAPCMD(buffer_set_mark);
BUFFER_WRAPCMD(buffer_clear_mark);
BUFFER_WRAPCMD(buffer_copy);
BUFFER_WRAPCMD(buffer_cut);
BUFFER_WRAPCMD(buffer_paste);
BUFFER_WRAPCMD(buffer_paste_older);
BUFFER_WRAPCMD(buffer_goto_beginning);
BUFFER_WRAPCMD(buffer_goto_end);
BUFFER_WRAPCMD(buffer_undo);

static struct command BUFFER_COMMANDS[] = {
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
};

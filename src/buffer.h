#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "command.h"
#include "text.h"
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

/**
 * A line render hook
 *
 * A callback paired with userdata
 */
struct line_render_hook {
  line_render_cb callback;
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

/**
 * A buffer of text that can be modified, read from and written to disk.
 *
 * This is the central data structure of dged and most other behavior is
 * implemented on top of it.
 */
struct buffer {
  char *name;
  char *filename;

  struct text *text;

  uint32_t dot_line;
  uint32_t dot_col;

  // local keymaps
  struct keymap *keymaps;
  uint32_t nkeymaps;
  uint32_t nkeymaps_max;

  uint32_t scroll_line;
  uint32_t scroll_col;

  struct update_hooks update_hooks;
};

struct buffer buffer_create(char *name, bool modeline);
void buffer_destroy(struct buffer *buffer);

uint32_t buffer_keymaps(struct buffer *buffer, struct keymap **keymaps_out);
void buffer_add_keymap(struct buffer *buffer, struct keymap *keymap);

int buffer_add_text(struct buffer *buffer, uint8_t *text, uint32_t nbytes);
void buffer_clear(struct buffer *buffer);
bool buffer_is_empty(struct buffer *buffer);

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

struct text_chunk buffer_get_line(struct buffer *buffer, uint32_t line);

uint32_t buffer_add_update_hook(struct buffer *buffer, update_hook_cb hook,
                                void *userdata);

struct buffer buffer_from_file(char *filename);
void buffer_to_file(struct buffer *buffer);

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
};

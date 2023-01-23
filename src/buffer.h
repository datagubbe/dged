#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "command.h"
#include "text.h"
#include "window.h"

struct keymap;
struct reactor;
struct command_list;

struct margin {
  uint32_t left;
  uint32_t right;
  uint32_t top;
  uint32_t bottom;
};

typedef void (*line_render_cb)(struct text_chunk *line_data, uint32_t line,
                               struct command_list *commands, void *userdata);

struct line_render_hook {
  line_render_cb callback;
  void *userdata;
};

struct update_hook_result {
  struct margin margins;
  struct line_render_hook line_render_hook;
};

typedef struct update_hook_result (*update_hook_cb)(
    struct buffer *buffer, struct command_list *commands, uint32_t width,
    uint32_t height, uint64_t frame_time, void *userdata);

struct update_hook {
  update_hook_cb callback;
  void *userdata;
};

struct update_hooks {
  struct update_hook hooks[32];
  uint32_t nhooks;
};

struct modeline {
  uint8_t *buffer;
};

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
void buffer_forward_char(struct buffer *buffer);
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
BUFFER_WRAPCMD(buffer_forward_char);
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
    {.name = "forward-char", .fn = buffer_forward_char_cmd},
    {.name = "backward-line", .fn = buffer_backward_line_cmd},
    {.name = "forward-line", .fn = buffer_forward_line_cmd},
    {.name = "end-of-line", .fn = buffer_end_of_line_cmd},
    {.name = "beginning-of-line", .fn = buffer_beginning_of_line_cmd},
    {.name = "newline", .fn = buffer_newline_cmd},
    {.name = "indent", .fn = buffer_indent_cmd},
    {.name = "buffer-write-to-file", .fn = buffer_to_file_cmd},
};

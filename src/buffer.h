#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "command.h"
#include "text.h"

struct keymap;
struct reactor;

struct buffer {
  const char *name;
  const char *filename;

  struct text *text;

  uint32_t dot_line;
  uint32_t dot_col;

  uint8_t *modeline_buf;

  // local keymaps
  struct keymap *keymaps;
  uint32_t nkeymaps;
  uint32_t nkeymaps_max;

  uint32_t scroll_line;
  uint32_t scroll_col;
};

struct buffer_update {
  struct render_cmd *cmds;
  uint64_t ncmds;
  uint32_t dot_col;
  uint32_t dot_line;
};

typedef void *(alloc_fn)(size_t);

struct buffer buffer_create(const char *name);
void buffer_destroy(struct buffer *buffer);

uint32_t buffer_keymaps(struct buffer *buffer, struct keymap **keymaps_out);
void buffer_add_keymap(struct buffer *buffer, struct keymap *keymap);

int buffer_add_text(struct buffer *buffer, uint8_t *text, uint32_t nbytes);

void buffer_forward_delete_char(struct buffer *buffer);
void buffer_backward_delete_char(struct buffer *buffer);
void buffer_backward_char(struct buffer *buffer);
void buffer_forward_char(struct buffer *buffer);
void buffer_backward_line(struct buffer *buffer);
void buffer_forward_line(struct buffer *buffer);
void buffer_end_of_line(struct buffer *buffer);
void buffer_beginning_of_line(struct buffer *buffer);
void buffer_newline(struct buffer *buffer);

uint32_t buffer_add_pre_update_hook(struct buffer *buffer);
uint32_t buffer_add_post_update_hook(struct buffer *buffer);
uint32_t buffer_remove_pre_update_hook(struct buffer *buffer, uint32_t hook_id);
uint32_t buffer_remove_post_update_hook(struct buffer *buffer,
                                        uint32_t hook_id);

struct buffer buffer_from_file(const char *filename, struct reactor *reactor);
void buffer_to_file(struct buffer *buffer);

struct buffer_update buffer_update(struct buffer *buffer, uint32_t width,
                                   uint32_t height, alloc_fn frame_alloc,
                                   struct reactor *reactor,
                                   uint64_t frame_time);

// commands
#define BUFFER_WRAPCMD(fn)                                                     \
  static void fn##_cmd(struct command_ctx ctx, int argc, const char *argv[]) { \
    fn(ctx.current_buffer);                                                    \
  }

BUFFER_WRAPCMD(buffer_backward_delete_char);
BUFFER_WRAPCMD(buffer_backward_char);
BUFFER_WRAPCMD(buffer_forward_char);
BUFFER_WRAPCMD(buffer_backward_line);
BUFFER_WRAPCMD(buffer_forward_line);
BUFFER_WRAPCMD(buffer_end_of_line);
BUFFER_WRAPCMD(buffer_beginning_of_line);
BUFFER_WRAPCMD(buffer_newline)
BUFFER_WRAPCMD(buffer_to_file);

static struct command BUFFER_COMMANDS[] = {
    {.name = "backward-delete-char", .fn = buffer_backward_delete_char_cmd},
    {.name = "backward-char", .fn = buffer_backward_char_cmd},
    {.name = "forward-char", .fn = buffer_forward_char_cmd},
    {.name = "backward-line", .fn = buffer_backward_line_cmd},
    {.name = "forward-line", .fn = buffer_forward_line_cmd},
    {.name = "end-of-line", .fn = buffer_end_of_line_cmd},
    {.name = "beginning-of-line", .fn = buffer_beginning_of_line_cmd},
    {.name = "newline", .fn = buffer_newline_cmd},
    {.name = "buffer-write-to-file", .fn = buffer_to_file_cmd},
};

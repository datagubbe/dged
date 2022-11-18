#include <stddef.h>
#include <stdint.h>

#include "command.h"
#include "text.h"

struct keymap;

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

  uint32_t lines_rendered;
};

struct buffer_update {
  struct render_cmd *cmds;
  uint64_t ncmds;
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

struct buffer buffer_from_file(const char *filename);
int buffer_to_file(struct buffer *buffer);

struct buffer_update buffer_begin_frame(struct buffer *buffer, uint32_t width,
                                        uint32_t height, alloc_fn frame_alloc);
void buffer_end_frame(struct buffer *buffer, struct buffer_update *upd);

static struct command BUFFER_COMMANDS[] = {
    {.name = "backward-delete-char", .fn = buffer_backward_delete_char},
    {.name = "backward-char", .fn = buffer_backward_char},
    {.name = "forward-char", .fn = buffer_forward_char},
    {.name = "backward-line", .fn = buffer_backward_line},
    {.name = "forward-line", .fn = buffer_forward_line},
    {.name = "end-of-line", .fn = buffer_end_of_line},
    {.name = "beginning-of-line", .fn = buffer_beginning_of_line},
    {.name = "newline", .fn = buffer_newline},
};

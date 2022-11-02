#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// opaque so it is easier to change representation to gap, rope etc.
struct text;

struct render_cmd;

struct text *text_create(uint32_t initial_capacity);
void text_destroy(struct text *text);

void text_append(struct text *text, uint32_t line, uint32_t col, uint8_t *bytes,
                 uint32_t nbytes, uint32_t *lines_added, uint32_t *cols_added);

void text_delete(struct text *text, uint32_t line, uint32_t col,
                 uint32_t nchars);
void text_delete_line(struct text *text, uint32_t line);

uint32_t text_render(struct text *text, uint32_t line, uint32_t nlines,
                     struct render_cmd *cmds, uint32_t max_ncmds);

uint32_t text_num_lines(struct text *text);
uint32_t text_line_length(struct text *text, uint32_t lineidx);
uint32_t text_line_size(struct text *text, uint32_t lineidx);

struct txt_line {
  uint8_t *text;
  uint32_t nbytes;
  uint32_t nchars;
};

typedef void (*line_cb)(struct txt_line *line);
void text_for_each_line(struct text *text, uint32_t line, uint32_t nlines,
                        line_cb callback);

struct txt_line text_get_line(struct text *text, uint32_t line);

bool text_line_contains_unicode(struct text *text, uint32_t line);

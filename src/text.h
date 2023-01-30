#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// opaque so it is easier to change representation to gap, rope etc.
struct text;

struct render_command;

struct text *text_create(uint32_t initial_capacity);
void text_destroy(struct text *text);

/**
 * Clear the text without reclaiming memory
 */
void text_clear(struct text *text);

void text_insert_at(struct text *text, uint32_t line, uint32_t col,
                    uint8_t *bytes, uint32_t nbytes, uint32_t *lines_added,
                    uint32_t *cols_added);

void text_append(struct text *text, uint8_t *bytes, uint32_t nbytes,
                 uint32_t *lines_added, uint32_t *cols_added);

void text_delete(struct text *text, uint32_t start_line, uint32_t start_col,
                 uint32_t end_line, uint32_t end_col);

uint32_t text_num_lines(struct text *text);
uint32_t text_line_length(struct text *text, uint32_t lineidx);
uint32_t text_line_size(struct text *text, uint32_t lineidx);
uint32_t text_col_to_byteindex(struct text *text, uint32_t line, uint32_t col);
uint32_t text_byteindex_to_col(struct text *text, uint32_t line,
                               uint32_t byteindex);

struct text_chunk {
  uint8_t *text;
  uint32_t nbytes;
  uint32_t nchars;
  uint32_t line;
};

typedef void (*chunk_cb)(struct text_chunk *chunk, void *userdata);
void text_for_each_line(struct text *text, uint32_t line, uint32_t nlines,
                        chunk_cb callback, void *userdata);

void text_for_each_chunk(struct text *text, chunk_cb callback, void *userdata);

struct text_chunk text_get_line(struct text *text, uint32_t line);
struct text_chunk text_get_region(struct text *text, uint32_t start_line,
                                  uint32_t start_col, uint32_t end_line,
                                  uint32_t end_col);

bool text_line_contains_unicode(struct text *text, uint32_t line);

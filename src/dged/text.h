#ifndef _TEXT_H
#define _TEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "location.h"

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
uint32_t text_global_idx(struct text *text, uint32_t line, uint32_t col);

struct text_chunk {
  uint8_t *text;
  uint32_t nbytes;
  uint32_t nchars;
  uint32_t line;
  bool allocated;
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

enum text_property_type {
  TextProperty_Colors,
};

struct text_property_colors {
  bool set_fg;
  uint32_t fg;
  bool set_bg;
  uint32_t bg;
};

struct text_property {
  enum text_property_type type;
  union {
    struct text_property_colors colors;
  };
};

void text_add_property(struct text *text, struct location start,
                       struct location end, struct text_property property);

void text_get_properties(struct text *text, struct location location,
                         struct text_property **properties,
                         uint32_t max_nproperties, uint32_t *nproperties);

void text_clear_properties(struct text *text);

#endif

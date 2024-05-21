#ifndef _TEXT_H
#define _TEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "location.h"
#include "utf8.h"

struct text;

struct text_chunk {
  uint8_t *text;
  uint32_t nbytes;
  uint32_t line;
  bool allocated;
};

struct text *text_create(uint32_t initial_capacity);
void text_destroy(struct text *text);

/**
 * Clear the text without reclaiming memory
 */
void text_clear(struct text *text);

void text_insert_at(struct text *text, uint32_t line, uint32_t offset,
                    uint8_t *bytes, uint32_t nbytes, uint32_t *lines_added);

void text_append(struct text *text, uint8_t *bytes, uint32_t nbytes,
                 uint32_t *lines_added);

void text_delete(struct text *text, uint32_t start_line, uint32_t start_offset,
                 uint32_t end_line, uint32_t end_offset);

uint32_t text_num_lines(const struct text *text);
uint32_t text_line_size(const struct text *text, uint32_t lineidx);
struct utf8_codepoint_iterator
text_line_codepoint_iterator(const struct text *text, uint32_t lineidx);
struct utf8_codepoint_iterator
text_chunk_codepoint_iterator(const struct text_chunk *chunk);

typedef void (*chunk_cb)(struct text_chunk *chunk, void *userdata);
void text_for_each_line(struct text *text, uint32_t line, uint32_t nlines,
                        chunk_cb callback, void *userdata);

void text_for_each_chunk(struct text *text, chunk_cb callback, void *userdata);

struct text_chunk text_get_line(struct text *text, uint32_t line);
struct text_chunk text_get_region(struct text *text, uint32_t start_line,
                                  uint32_t start_offset, uint32_t end_line,
                                  uint32_t end_offset);

enum text_property_type {
  TextProperty_Colors,
  TextProperty_Data,
};

struct text_property_colors {
  bool set_fg;
  uint32_t fg;
  bool set_bg;
  uint32_t bg;
};

struct text_property {
  enum text_property_type type;
  union property_data {
    struct text_property_colors colors;
    void *userdata;
  } data;
};

void text_add_property(struct text *text, uint32_t start_line,
                       uint32_t start_offset, uint32_t end_line,
                       uint32_t end_offset, struct text_property property);

void text_get_properties(struct text *text, uint32_t line, uint32_t offset,
                         struct text_property **properties,
                         uint32_t max_nproperties, uint32_t *nproperties);

void text_clear_properties(struct text *text);

#endif

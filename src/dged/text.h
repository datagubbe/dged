#ifndef _TEXT_H
#define _TEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "location.h"
#include "utf8.h"

struct text;

enum line_endings {
  LineEnding_LF,
  LineEnding_CRLF,
};

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

enum line_endings text_get_line_ending(const struct text *);

size_t text_size(const struct text *text);
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

struct text_chunk text_get(struct text *text, void *(*alloc)(size_t size));

enum text_property_type {
  TextProperty_Colors,
  TextProperty_Data,
};

struct text_property_colors {
  bool set_fg;
  uint32_t fg;
  bool set_bg;
  uint32_t bg;
  bool underline;
  bool inverted;
};

struct text_property {
  struct location start;
  struct location end;
  enum text_property_type type;
  union property_data {
    struct text_property_colors colors;
    void *userdata;
  } data;
};

typedef uint64_t layer_id;
enum layer_ids {
  PropertyLayer_Default = 0,
};

void text_add_property(struct text *text, uint32_t start_line,
                       uint32_t start_offset, uint32_t end_line,
                       uint32_t end_offset, struct text_property property);

void text_add_property_to_layer(struct text *text, uint32_t start_line,
                                uint32_t start_offset, uint32_t end_line,
                                uint32_t end_offset,
                                struct text_property property, layer_id layer);

layer_id text_add_property_layer(struct text *text);
void text_remove_property_layer(struct text *text, layer_id layer);

void text_get_properties(struct text *text, uint32_t line, uint32_t offset,
                         struct text_property **properties,
                         uint32_t max_nproperties, uint32_t *nproperties);

void text_get_properties_filtered(struct text *text, uint32_t line,
                                  uint32_t offset,
                                  struct text_property **properties,
                                  uint32_t max_nproperties,
                                  uint32_t *nproperties, layer_id layer);

void text_clear_properties(struct text *text);
void text_clear_property_layer(struct text *text, layer_id layer);

#endif

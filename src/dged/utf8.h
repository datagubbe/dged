#ifndef _UTF8_H
#define _UTF8_H

#include <stdbool.h>
#include <stdint.h>

struct codepoint {
  uint32_t codepoint;
  uint32_t nbytes;
};

struct utf8_codepoint_iterator {
  uint8_t *data;
  uint64_t nbytes;
  uint64_t offset;
  struct codepoint current;
};

struct utf8_codepoint_iterator
create_utf8_codepoint_iterator(uint8_t *data, uint64_t len,
                               uint64_t initial_offset);
struct codepoint *utf8_next_codepoint(struct utf8_codepoint_iterator *iter);

/*!
 * \brief Return the number of chars the utf-8 sequence pointed at by `bytes` of
 * length `nbytes`, represents
 */
uint32_t utf8_nchars(uint8_t *bytes, uint32_t nbytes);

uint32_t unicode_visual_char_width(const struct codepoint *codepoint);

bool utf8_byte_is_unicode_start(uint8_t byte);
bool utf8_byte_is_unicode_continuation(uint8_t byte);
bool utf8_byte_is_unicode(uint8_t byte);
bool utf8_byte_is_ascii(uint8_t byte);

#endif

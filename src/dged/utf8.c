#include "utf8.h"

#include <assert.h>
#include <stdio.h>
#include <wchar.h>

bool utf8_byte_is_unicode_start(uint8_t byte) { return (byte & 0xc0) == 0xc0; }
bool utf8_byte_is_unicode_continuation(uint8_t byte) {
  return utf8_byte_is_unicode(byte) && !utf8_byte_is_unicode_start(byte);
}
bool utf8_byte_is_unicode(uint8_t byte) { return (byte & 0x80) != 0x0; }
bool utf8_byte_is_ascii(uint8_t byte) { return !utf8_byte_is_unicode(byte); }

enum utf8_state {
  Utf8_Accept = 0,
  Utf8_Reject = 1,
};

// clang-format off
static const uint8_t utf8d[] = {
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 00..1f
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 20..3f
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 40..5f
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 60..7f
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, // 80..9f
  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7, // a0..bf
  8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, // c0..df
  0xa,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x4,0x3,0x3, // e0..ef
  0xb,0x6,0x6,0x6,0x5,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8, // f0..ff
  0x0,0x1,0x2,0x3,0x5,0x8,0x7,0x1,0x1,0x1,0x4,0x6,0x1,0x1,0x1,0x1, // s0..s0
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,0,1,0,1,1,1,1,1,1, // s1..s2
  1,2,1,1,1,1,1,2,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1, // s3..s4
  1,2,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,3,1,3,1,1,1,1,1,1, // s5..s6
  1,3,1,1,1,1,1,3,1,3,1,1,1,1,1,1,1,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1, // s7..s8
};
// clang-format on

/*
 * emoji decoding algorithm from
 * https://bjoern.hoehrmann.de/utf-8/decoder/dfa/
 */
static enum utf8_state decode(enum utf8_state *state, uint32_t *codep,
                              uint32_t byte) {
  uint32_t type = utf8d[byte];

  *codep = (*state != Utf8_Accept) ? (byte & 0x3fu) | (*codep << 6)
                                   : (0xff >> type) & (byte);

  *state = utf8d[256 + *state * 16 + type];
  return *state;
}

static struct codepoint next_utf8_codepoint(uint8_t *bytes, uint64_t nbytes) {
  uint32_t codepoint = 0;
  enum utf8_state state = Utf8_Accept;
  uint32_t bi = 0;
  while (bi < nbytes) {
    enum utf8_state res = decode(&state, &codepoint, bytes[bi]);
    ++bi;

    if (res == Utf8_Accept || res == Utf8_Reject) {
      break;
    }
  }

  if (state == Utf8_Reject) {
    codepoint = 0xfffd;
  }

  return (struct codepoint){.codepoint = codepoint, .nbytes = bi};
}

struct codepoint *utf8_next_codepoint(struct utf8_codepoint_iterator *iter) {
  if (iter->offset >= iter->nbytes) {
    return NULL;
  }

  iter->current = next_utf8_codepoint(iter->data + iter->offset,
                                      iter->nbytes - iter->offset);
  iter->offset += iter->current.nbytes;
  return &iter->current;
}

struct utf8_codepoint_iterator
create_utf8_codepoint_iterator(uint8_t *data, uint64_t len,
                               uint64_t initial_offset) {
  return (struct utf8_codepoint_iterator){
      .data = data,
      .nbytes = len,
      .offset = initial_offset,
  };
}

/* TODO: grapheme clusters and other classification, this
 * returns the number of unicode code points
 */
uint32_t utf8_nchars(uint8_t *bytes, uint32_t nbytes) {
  uint32_t bi = 0;
  uint32_t nchars = 0;
  while (bi < nbytes) {
    struct codepoint codepoint = next_utf8_codepoint(bytes + bi, nbytes - bi);
    ++nchars;
    bi += codepoint.nbytes;
  }

  return nchars;
}

/* TODO: grapheme clusters and other classification, this
 * returns the number of unicode code points
 */
uint32_t utf8_nbytes(uint8_t *bytes, uint32_t nbytes, uint32_t nchars) {
  uint32_t bi = 0;
  uint32_t chars = 0;
  uint32_t expected = 0;

  while (chars < nchars && bi < nbytes) {
    struct codepoint codepoint = next_utf8_codepoint(bytes + bi, nbytes - bi);
    bi += codepoint.nbytes;
    ++chars;
  }

  // TODO: reject invalid?
  return bi;
}

uint32_t unicode_visual_char_width(const struct codepoint *codepoint) {
  if (codepoint->nbytes > 0) {
    // TODO: use unicode classification instead
    size_t w = wcwidth(codepoint->codepoint);
    return w >= 0 ? w : 2;
  } else {
    return 0;
  }
}

#include "utf8.h"

#include <stdio.h>

bool utf8_byte_is_unicode_start(uint8_t byte) { return (byte & 0xc0) == 0xc0; }
bool utf8_byte_is_unicode_continuation(uint8_t byte) {
  return utf8_byte_is_unicode(byte) && !utf8_byte_is_unicode_start(byte);
}
bool utf8_byte_is_unicode(uint8_t byte) { return (byte & 0x80) != 0x0; }
bool utf8_byte_is_ascii(uint8_t byte) { return !utf8_byte_is_unicode(byte); }

// TODO: grapheme clusters, this returns the number of unicode code points
uint32_t utf8_nchars(uint8_t *bytes, uint32_t nbytes) {
  uint32_t nchars = 0;
  for (uint32_t bi = 0; bi < nbytes; ++bi) {
    if (utf8_byte_is_ascii(bytes[bi]) || utf8_byte_is_unicode_start(bytes[bi]))
      ++nchars;
  }
  return nchars;
}

// TODO: grapheme clusters, this uses the number of unicode code points
uint32_t utf8_nbytes(uint8_t *bytes, uint32_t nchars) {
  uint32_t bi = 0;
  uint32_t chars = 0;
  while (chars < nchars) {
    uint8_t byte = bytes[bi];
    if (utf8_byte_is_unicode_start(byte)) {
      ++chars;

      // length of char is the number of leading ones
      // flip it and count number of leading zeros
      uint8_t invb = ~byte;
      bi += __builtin_clz((uint32_t)invb) - 24;
    } else {
      ++chars;
      ++bi;
    }
  }

  return bi;
}

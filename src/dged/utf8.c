#include "utf8.h"

#include <stdio.h>
#include <wchar.h>

bool utf8_byte_is_unicode_start(uint8_t byte) { return (byte & 0xc0) == 0xc0; }
bool utf8_byte_is_unicode_continuation(uint8_t byte) {
  return utf8_byte_is_unicode(byte) && !utf8_byte_is_unicode_start(byte);
}
bool utf8_byte_is_unicode(uint8_t byte) { return (byte & 0x80) != 0x0; }
bool utf8_byte_is_ascii(uint8_t byte) { return !utf8_byte_is_unicode(byte); }

uint32_t utf8_nbytes_in_char(uint8_t byte) {
  // length of char is the number of leading ones
  // flip it and count number of leading zeros
  uint8_t invb = ~byte;
  return __builtin_clz((uint32_t)invb) - 24;
}

// TODO: grapheme clusters, this returns the number of unicode code points
uint32_t utf8_nchars(uint8_t *bytes, uint32_t nbytes) {
  uint32_t nchars = 0;
  uint32_t expected = 0;
  for (uint32_t bi = 0; bi < nbytes; ++bi) {
    uint8_t byte = bytes[bi];
    if (utf8_byte_is_unicode(byte)) {
      if (utf8_byte_is_unicode_start(byte)) {
        expected = utf8_nbytes_in_char(byte) - 1;
      } else { // continuation byte
        --expected;
        if (expected == 0) {
          ++nchars;
        }
      }
    } else { // ascii
      ++nchars;
    }
  }
  return nchars;
}

// TODO: grapheme clusters, this uses the number of unicode code points
uint32_t utf8_nbytes(uint8_t *bytes, uint32_t nbytes, uint32_t nchars) {

  uint32_t bi = 0;
  uint32_t chars = 0;
  uint32_t expected = 0;

  while (chars < nchars && bi < nbytes) {
    uint8_t byte = bytes[bi];
    if (utf8_byte_is_unicode(byte)) {
      if (utf8_byte_is_unicode_start(byte)) {
        expected = utf8_nbytes_in_char(byte) - 1;
      } else { // continuation char
        --expected;
        if (expected == 0) {
          ++chars;
        }
      }
    } else { // ascii
      ++chars;
    }

    ++bi;
  }

  return bi;
}

uint32_t utf8_visual_char_width(uint8_t *bytes, uint32_t len) {
  if (utf8_byte_is_unicode_start(*bytes)) {
    wchar_t wc;
    size_t nbytes = 0;
    if ((nbytes = mbrtowc(&wc, (char *)bytes, len, NULL)) > 0) {
      size_t w = wcwidth(wc);
      return w > 0 ? w : 2;
    } else {
      return 1;
    }
  } else if (utf8_byte_is_unicode_continuation(*bytes)) {
    return 0;
  } else {
    return 1;
  }
}

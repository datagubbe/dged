#include <stdbool.h>
#include <stdint.h>

/*!
 * \brief Return the number of chars the utf-8 sequence pointed at by `bytes` of
 * length `nbytes`, represents
 */
uint32_t utf8_nchars(uint8_t *bytes, uint32_t nbytes);

/* Return the number of bytes used to make up the next `nchars` characters */
uint32_t utf8_nbytes(uint8_t *bytes, uint32_t nbytes, uint32_t nchars);

/* true if `byte` is a unicode byte sequence start byte */
bool utf8_byte_is_unicode_start(uint8_t byte);
bool utf8_byte_is_unicode_continuation(uint8_t byte);
bool utf8_byte_is_ascii(uint8_t byte);
bool utf8_byte_is_unicode(uint8_t byte);

uint32_t utf8_visual_char_width(uint8_t *bytes, uint32_t len);

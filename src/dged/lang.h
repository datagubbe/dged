#ifndef _LANG_H
#define _LANG_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Settings for a programming language.
 */
struct language {
  /** Language id */
  const char *id;

  /** Descriptive name of the programming language */
  const char *name;

  /** Tab width for indentation */
  uint32_t tab_width;

  /** Path to the language server */
  const char *lang_srv;
};

/**
 * Initialize languages.
 *
 * @param[in] register_default Set to true to register some well known
 * languages.
 */
void languages_init(bool register_default);

/**
 * Get a language config by file name.
 *
 * @param[in] filename File name.
 * @returns A language config instance or the default language if not found.
 */
struct language lang_from_filename(const char *filename);

/**
 * Get a language config by id. The language id is a short (all-lowercase)
 * string identifying the language.
 *
 * @param[in] id The language id.
 * @returns A language config instance or the default language if not found.
 */
struct language lang_from_id(const char *id);

#endif

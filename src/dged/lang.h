#ifndef _LANG_H
#define _LANG_H

#include <stdbool.h>
#include <stdint.h>

struct setting;
struct setting_value;

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
 * Free up resources associated with a language.
 */
void lang_destroy(struct language *lang);

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

/**
 * Get all settings associated with a language.
 *
 * @param lang The language to get settings for.
 * @param settings Result array for settings.
 * @param nsettings resulting number of settings placed in @ref settings.
 */
void lang_settings(struct language *lang, struct setting **settings[],
                   uint32_t *nsettings);

/**
 * Get a single setting for a language.
 *
 * @param lang The language to get setting for.
 * @param key The setting key, relative to the language.
 *
 * @returns The setting if found, else NULL.
 */
struct setting *lang_setting(struct language *lang, const char *key);

/**
 * Set a setting for a language.
 *
 * @param lang The language to set for.
 * @param key The setting key, relative to the language.
 * @param value The value to set
 */
void lang_setting_set(struct language *lang, const char *key,
                      struct setting_value value);

/**
 * Set a default value for a language setting.
 *
 * @param lang The language to set for.
 * @param key The setting key, relative to the language.
 * @param value The value to set
 */
void lang_setting_set_default(struct language *lang, const char *key,
                              struct setting_value value);

#endif

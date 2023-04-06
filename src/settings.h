#include "hashmap.h"

#include <stdbool.h>
#include <stdint.h>

struct commands;

/**
 * The type of setting value.
 */
enum setting_type {
  /** String setting. */
  Setting_String = 0,

  /** Number setting (a signed 64 bit integer). */
  Setting_Number,

  /** Boolean setting. */
  Setting_Bool,
};

/**
 * Value for a setting.
 */
struct setting_value {
  /** Type of setting. */
  enum setting_type type;

  union {
    /** String setting. */
    char *string_value;

    /** Real number setting. */
    int64_t number_value;

    /** Boolean setting value. */
    bool bool_value;
  };
};

/**
 * A single setting.
 *
 * A setting has a "path", denoted by a string
 * containing a number (0-) of dots.
 * Example: editor.tab-width.
 */
struct setting {

  /** Path of the setting. */
  char path[128];

  /** Value of the setting. */
  struct setting_value value;
};

HASHMAP_ENTRY_TYPE(setting_entry, struct setting);

/**
 * A collection of settings.
 */
struct settings {
  HASHMAP(struct setting_entry) settings;
};

/**
 * Initialize the global collection of settings.
 *
 * @param initial_capacity Initial capacity of the settings collection.
 * @returns Nothing, the settings collection is a global instance.
 */
void settings_init(uint32_t initial_capacity, struct commands *commands);

/**
 * Destroy the global collection of settings.
 */
void settings_destroy();

/**
 * Register a new setting.
 *
 * @param path The path of the new setting on
 * the form <category>.<sub-category>.<setting-name>.
 * @param default_value The default value for the setting.
 * All settings are required to declare a default value.
 */
void settings_register_setting(const char *path,
                               struct setting_value default_value);

/**
 * Retrieve a single setting by path.
 *
 * @param path The exact path of the setting on
 * the form <category>.<sub-category>.<setting-name>.
 * @returns A pointer to the setting if found, NULL otherwise.
 */
struct setting *settings_get(const char *path);

/**
 * Retrieve a collection of settings by prefix
 *
 * @param prefix Path prefix for the settings to retrieve.
 * @param settings_out Pointer to an array that will be modified to point to the
 * result.
 * @param nsettings_out Pointer to an integer that will be set to the number of
 * settings pointers in the result.
 */
void settings_get_prefix(const char *prefix, struct setting **settings_out[],
                         uint32_t *nsettings_out);

/**
 * Set a value for a setting.
 *
 * @param path The exact path of the setting on
 * the form <category>.<sub-category>.<setting-name>.
 * @param value The new value of the setting. The type has to match the declared
 * type for the setting. If not, the new value is ignored.
 */
void settings_set(const char *path, struct setting_value value);

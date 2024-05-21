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

  union setting_data {
    /** String setting. */
    char *string_value;

    /** Real number setting. */
    int64_t number_value;

    /** Boolean setting value. */
    bool bool_value;
  } data;
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
 */
void settings_init(uint32_t initial_capacity);

/**
 * Destroy the global collection of settings.
 */
void settings_destroy(void);

/**
 * Retrieve a single setting by path.
 *
 * @param path The exact path of the setting on
 * the form @code <category>.<sub-category>.<setting-name> @endcode.
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
 * the form @code <category>.<sub-category>.<setting-name> @endcode.
 * @param value The new value of the setting. The type has to match the declared
 * type for the setting. If not, the new value is ignored.
 */
void settings_set(const char *path, struct setting_value value);

/**
 * Set the default value for a setting.
 *
 * This works the same as @ref settings_set but
 * will not overwrite the value if the setting already has one.
 *
 * @param path The exact path of the setting on
 * the form @code <category>.<sub-category>.<setting-name> @code.
 * @param value The new value of the setting. The type has to match the declared
 * type for the setting. If not, the new value is ignored.
 */
void settings_set_default(const char *path, struct setting_value value);

/**
 * Set a value for a setting.
 *
 * @param setting Pointer to a setting to set.
 * @param val The new value of the setting. The type has to match the declared
 * type for the setting. If not, the new value is ignored.
 */
void setting_set_value(struct setting *setting, struct setting_value val);

/**
 * Create a string representation for a setting.
 *
 * @param setting Pointer to a setting to turn into a string.
 * @param buf Character buffer to store resulting string in.
 * @param n Size in bytes of @p buf.
 */
void setting_to_string(struct setting *setting, char *buf, size_t n);

const char *setting_join_key(const char *initial, const char *setting);

/**
 * Parse settings from a string in TOML format.
 *
 * @param toml Pointer to a NULL-terminated string containing TOML settings.
 * @param errmsgs Pointer to a string array where error messages will be placed.
 * These messages must be freed after use.
 * @returns 0 on success, n > 0 where n denotes the number of error messages in
 * @p errmsgs
 */
int32_t settings_from_string(const char *toml, char **errmsgs[]);

/**
 * Parse settings from a file in TOML format.
 *
 * @param path Path to a TOML file containing settings.
 * @param errmsgs Pointer to a string array where error messages will be placed.
 * These messages must be freed after use.
 * @returns 0 on success, n > 0 where n denotes the number of error messages in
 * @p errmsgs
 */
int32_t settings_from_file(const char *path, char **errmsgs[]);

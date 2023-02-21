#include "command.h"

#include <stdbool.h>
#include <stdint.h>

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

  /** Hashed path that can be used for equality checks. */
  uint32_t hash;

  /** Value of the setting. */
  struct setting_value value;
};

/**
 * A collection of settings.
 */
struct settings {
  /** Settings */
  struct setting *settings;

  /** Number of settings currently in collection. */
  uint32_t nsettings;

  /** Current capacity of collection. */
  uint32_t capacity;
};

/**
 * Initialize the global collection of settings.
 *
 * @param initial_capacity Initial capacity of the settings collection.
 * @returns Nothing, the settings collection is a global instance.
 */
void settings_init(uint32_t initial_capacity);

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

int32_t settings_get_cmd(struct command_ctx ctx, int argc, const char *argv[]);
int32_t settings_set_cmd(struct command_ctx ctx, int argc, const char *argv[]);

static struct command SETTINGS_COMMANDS[] = {
    {.name = "set", .fn = settings_set_cmd},
    {.name = "get", .fn = settings_get_cmd},
};

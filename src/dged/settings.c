#include "settings.h"
#include "command.h"
#include "hash.h"
#include "hashmap.h"
#include "minibuffer.h"
#include "vec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct settings g_settings = {0};

void settings_init(uint32_t initial_capacity) {
  HASHMAP_INIT(&g_settings.settings, initial_capacity, hash_name);
}

void settings_destroy() {
  HASHMAP_FOR_EACH(&g_settings.settings, struct setting_entry * entry) {
    struct setting *setting = &entry->value;
    if (setting->value.type == Setting_String) {
      free(setting->value.string_value);
    }
  }

  HASHMAP_DESTROY(&g_settings.settings);
}

void setting_set_value(struct setting *setting, struct setting_value val) {
  if (setting->value.type == val.type) {
    if (setting->value.type == Setting_String && val.string_value != NULL) {
      setting->value.string_value = strdup(val.string_value);
    } else {
      setting->value = val;
    }
  }
}

void settings_register_setting(const char *path,
                               struct setting_value default_value) {
  HASHMAP_APPEND(&g_settings.settings, struct setting_entry, path,
                 struct setting_entry * s);

  if (s != NULL) {
    struct setting *new_setting = &s->value;
    new_setting->value.type = default_value.type;
    setting_set_value(new_setting, default_value);
    strncpy(new_setting->path, path, 128);
    new_setting->path[127] = '\0';
  }
}

struct setting *settings_get(const char *path) {
  HASHMAP_GET(&g_settings.settings, struct setting_entry, path,
              struct setting * s);
  return s;
}

void settings_get_prefix(const char *prefix, struct setting **settings_out[],
                         uint32_t *nsettings_out) {

  uint32_t capacity = 16;
  VEC(struct setting *) res;
  VEC_INIT(&res, 16);
  HASHMAP_FOR_EACH(&g_settings.settings, struct setting_entry * entry) {
    struct setting *setting = &entry->value;
    if (strncmp(prefix, setting->path, strlen(prefix)) == 0) {
      VEC_PUSH(&res, setting);
    }
  }

  *nsettings_out = VEC_SIZE(&res);
  *settings_out = VEC_ENTRIES(&res);
}

void settings_set(const char *path, struct setting_value value) {
  struct setting *setting = settings_get(path);
  if (setting != NULL) {
    setting_set_value(setting, value);
  }
}

void setting_to_string(struct setting *setting, char *buf, size_t n) {
  switch (setting->value.type) {
  case Setting_Bool:
    snprintf(buf, n, "%s", setting->value.bool_value ? "true" : "false");
    break;
  case Setting_Number:
    snprintf(buf, n, "%ld", setting->value.number_value);
    break;
  case Setting_String:
    snprintf(buf, n, "%s", setting->value.string_value);
    break;
  }
}

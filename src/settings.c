#include "settings.h"
#include "command.h"
#include "hash.h"
#include "minibuffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct settings g_settings = {0};

void settings_resize(uint32_t new_capacity) {
  if (new_capacity > g_settings.capacity) {
    g_settings.settings =
        realloc(g_settings.settings, sizeof(struct setting) * new_capacity);
  }
}

void settings_init(uint32_t initial_capacity) {
  settings_resize(initial_capacity);
  g_settings.capacity = initial_capacity;
  g_settings.nsettings = 0;
}

void settings_destroy() {
  for (uint32_t i = 0; i < g_settings.nsettings; ++i) {
    struct setting *setting = &g_settings.settings[i];
    if (setting->value.type == Setting_String) {
      free(setting->value.string_value);
    }
  }

  free(g_settings.settings);
  g_settings.settings = NULL;
  g_settings.capacity = 0;
  g_settings.nsettings = 0;
}

void settings_register_setting(const char *path,
                               struct setting_value default_value) {
  if (g_settings.nsettings + 1 == g_settings.capacity) {
    g_settings.capacity *= 2;
    settings_resize(g_settings.capacity);
  }

  struct setting *s = &g_settings.settings[g_settings.nsettings];
  s->value = default_value;
  s->hash = hash_name(path);
  strncpy(s->path, path, 128);
  s->path[127] = '\0';

  ++g_settings.nsettings;
}

struct setting *settings_get(const char *path) {
  uint32_t needle = hash_name(path);

  for (uint32_t i = 0; i < g_settings.nsettings; ++i) {
    struct setting *setting = &g_settings.settings[i];
    if (setting->hash == needle) {
      return setting;
    }
  }

  return NULL;
}

void settings_get_prefix(const char *prefix, struct setting **settings_out[],
                         uint32_t *nsettings_out) {

  uint32_t capacity = 16;
  struct setting **res = malloc(sizeof(struct setting *) * capacity);
  uint32_t nsettings = 0;
  for (uint32_t i = 0; i < g_settings.nsettings; ++i) {
    struct setting *setting = &g_settings.settings[i];
    if (strncmp(prefix, setting->path, strlen(prefix)) == 0) {
      if (nsettings + 1 == capacity) {
        capacity *= 2;
        res = realloc(res, sizeof(struct setting *) * capacity);
      }

      res[nsettings] = setting;
      ++nsettings;
    }
  }

  *nsettings_out = nsettings;
  *settings_out = res;
}

void settings_set(const char *path, struct setting_value value) {
  struct setting *setting = settings_get(path);
  if (setting != NULL && setting->value.type == value.type) {
    setting->value = value;
  }
}

void setting_to_string(struct setting *setting, char *buf, size_t n) {
  switch (setting->value.type) {
  case Setting_Bool:
    snprintf(buf, n, "%s", setting->value.bool_value ? "true" : false);
    break;
  case Setting_Number:
    snprintf(buf, n, "%ld", setting->value.number_value);
    break;
  case Setting_String:
    snprintf(buf, n, "%s", setting->value.string_value);
    break;
  }
}

int32_t settings_get_cmd(struct command_ctx ctx, int argc, const char *argv[]) {
  if (argc == 0) {
    return minibuffer_prompt(ctx, "setting: ");
  }

  struct setting *setting = settings_get(argv[0]);
  if (setting == NULL) {
    minibuffer_echo_timeout(4, "no such setting \"%s\"", argv[0]);
    return 1;
  } else {
    char buf[128];
    setting_to_string(setting, buf, 128);
    minibuffer_echo("%s = %s", argv[0], buf);
  }

  return 0;
}

int32_t settings_set_cmd(struct command_ctx ctx, int argc, const char *argv[]) {
  if (argc == 0) {
    return minibuffer_prompt(ctx, "setting: ");
  } else if (argc == 1) {
    // validate setting here as well
    struct setting *setting = settings_get(argv[0]);
    if (setting == NULL) {
      minibuffer_echo_timeout(4, "no such setting \"%s\"", argv[0]);
      return 1;
    }

    command_ctx_push_arg(&ctx, argv[0]);
    return minibuffer_prompt(ctx, "value: ");
  }

  struct setting *setting = settings_get(argv[0]);
  if (setting == NULL) {
    minibuffer_echo_timeout(4, "no such setting \"%s\"", argv[0]);
    return 1;
  } else {
    const char *value = argv[1];
    struct setting_value new_value = {.type = setting->value.type};
    switch (setting->value.type) {
    case Setting_Bool:
      new_value.bool_value = strncmp("true", value, 4) == 0;
      break;
    case Setting_Number:
      new_value.number_value = atol(value);
      break;
    case Setting_String:
      new_value.string_value = strdup(value);
      break;
    }

    setting->value = new_value;
  }

  return 0;
}

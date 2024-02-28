#include "lang.h"
#include "minibuffer.h"
#include "settings.h"

#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct setting *_lang_setting(const char *id, const char *key);
static void _lang_setting_set(const char *id, const char *key,
                              struct setting_value value);
static void _lang_setting_set_default(const char *id, const char *key,
                                      struct setting_value value);

void define_lang(const char *name, const char *id, const char *pattern,
                 uint32_t tab_width) {

  _lang_setting_set_default(
      id, "name",
      (struct setting_value){.type = Setting_String,
                             .string_value = (char *)name});
  _lang_setting_set_default(
      id, "pattern",
      (struct setting_value){.type = Setting_String,
                             .string_value = (char *)pattern});
  _lang_setting_set_default(id, "tab-width",
                            (struct setting_value){.type = Setting_Number,
                                                   .number_value = tab_width});
}

static struct language g_fundamental = {
    .name = "Fundamental",
    .id = "fnd",
};

void languages_init(bool register_default) {
  if (register_default) {
    define_lang("Bash", "bash", "^.*\\.bash$", 4);
    define_lang("C", "c", "^.*\\.(c|h)$", 2);
    define_lang("C++", "cxx", "^.*\\.(cpp|cxx|cc|c++|hh|h)$", 2);
    define_lang("Rust", "rs", "^.*\\.rs$", 4);
    define_lang("Nix", "nix", "^.*\\.nix$", 2);
    define_lang("Make", "make", "^.*(Makefile|\\.mk)$", 4);
    define_lang("Python", "python", "^.*\\.py$", 4);
    define_lang("Git Commit Message", "gitcommit", "^.*COMMIT_EDITMSG$", 4);
  }
}

void lang_destroy(struct language *lang) {
  if (!lang_is_fundamental(lang)) {
    free((void *)lang->id);
  }
}

bool lang_is_fundamental(const struct language *lang) {
  return strlen(lang->id) == 3 && memcmp(lang->id, "fnd", 3) == 0;
}

static struct language lang_from_settings(const char *id) {
  struct setting *name = _lang_setting(id, "name");
  const char *name_value = name != NULL ? name->value.string_value : "Unknown";

  return (struct language){
      .id = strdup(id),
      .name = name_value,
  };
}

static void next_ext(const char *curr, const char **nxt, const char **end) {
  if (curr == NULL) {
    *nxt = *end = NULL;
    return;
  }

  *nxt = curr;
  *end = curr + strlen(curr);

  const char *spc = strchr(curr, ' ');
  if (spc != NULL) {
    *end = spc;
  }
}

void lang_settings(struct language *lang, struct setting **settings[],
                   uint32_t *nsettings) {
  const char *key = setting_join_key("languages", lang->id);
  settings_get_prefix(key, settings, nsettings);
  free((void *)key);
}

static struct setting *_lang_setting(const char *id, const char *key) {
  const char *langkey = setting_join_key("languages", id);
  const char *setting_key = setting_join_key(langkey, key);

  struct setting *res = settings_get(setting_key);

  free((void *)setting_key);
  free((void *)langkey);

  return res;
}

struct setting *lang_setting(struct language *lang, const char *key) {
  return _lang_setting(lang->id, key);
}

static void _lang_setting_set(const char *id, const char *key,
                              struct setting_value value) {
  const char *langkey = setting_join_key("languages", id);
  const char *setting_key = setting_join_key(langkey, key);

  settings_set(setting_key, value);

  free((void *)setting_key);
  free((void *)langkey);
}

void lang_setting_set(struct language *lang, const char *key,
                      struct setting_value value) {
  _lang_setting_set(lang->id, key, value);
}

static void _lang_setting_set_default(const char *id, const char *key,
                                      struct setting_value value) {
  const char *langkey = setting_join_key("languages", id);
  const char *setting_key = setting_join_key(langkey, key);

  settings_set_default(setting_key, value);

  free((void *)setting_key);
  free((void *)langkey);
}

void lang_setting_set_default(struct language *lang, const char *key,
                              struct setting_value value) {
  _lang_setting_set_default(lang->id, key, value);
}

struct language lang_from_filename(const char *filename) {

  if (strlen(filename) == 0) {
    return g_fundamental;
  }

  // get "languages.*" settings
  struct setting **settings = NULL;
  uint32_t nsettings = 0;
  settings_get_prefix("languages.", &settings, &nsettings);

  // find the first one with a matching regex
  for (uint32_t i = 0; i < nsettings; ++i) {
    struct setting *setting = settings[i];
    char *setting_name = strrchr(setting->path, '.');
    if (setting_name != NULL && strncmp(setting_name + 1, "pattern", 5) == 0) {
      const char *val = setting->value.string_value;
      regex_t regex;
      if (regcomp(&regex, val, REG_EXTENDED) == 0 &&
          regexec(&regex, filename, 0, NULL, 0) == 0) {

        // len of "languages."
        size_t id_len = setting_name - (setting->path + 10);
        char lang_id[128] = {0};
        memcpy(lang_id, setting->path + 10, id_len);
        lang_id[id_len] = '\0';

        regfree(&regex);
        free(settings);
        return lang_from_settings(lang_id);
      }
      regfree(&regex);
    }
  }

  free(settings);

  // fall back to fundamental
  return g_fundamental;
}

struct language lang_from_id(const char *id) {
  if (id == NULL || (strlen(id) == 3 && strncmp(id, "fnd", 3) == 0) ||
      strlen(id) == 0) {
    return g_fundamental;
  }

  // check that it exists
  struct setting **settings = NULL;
  uint32_t nsettings = 0;
  const char *lang_path = setting_join_key("languages", id);

  settings_get_prefix(lang_path, &settings, &nsettings);
  free(settings);

  if (nsettings > 0) {
    struct language l = lang_from_settings(id);
    free((void *)lang_path);
    return l;
  } else {
    minibuffer_echo_timeout(4, "failed to find language \"%s\"", id);
    free((void *)lang_path);
    return g_fundamental;
  }
}

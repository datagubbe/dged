#include "lang.h"
#include "minibuffer.h"
#include "settings.h"

#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void define_lang(const char *name, const char *id, const char *pattern,
                 uint32_t tab_width, const char *lang_srv) {
  char namebuf[128] = {0};

  const char *key = setting_join_key("languages", id);

  const char *pat_key = setting_join_key(key, "pattern");
  settings_set_default(pat_key,
                       (struct setting_value){.type = Setting_String,
                                              .string_value = (char *)pattern});
  free((void *)pat_key);

  const char *tabw_key = setting_join_key(key, "tab-width");
  settings_set_default(tabw_key,
                       (struct setting_value){.type = Setting_Number,
                                              .number_value = tab_width});
  free((void *)tabw_key);

  // TODO: move lang server
  if (lang_srv != NULL) {
    const char *langsrv_key = setting_join_key(key, "lang-srv");
    settings_set_default(
        langsrv_key, (struct setting_value){.type = Setting_String,
                                            .string_value = (char *)lang_srv});
    free((void *)langsrv_key);
  }

  const char *name_key = setting_join_key(key, "name");
  settings_set_default(name_key,
                       (struct setting_value){.type = Setting_String,
                                              .string_value = (char *)name});
  free((void *)name_key);
  free((void *)key);
}

static struct language g_fundamental = {
    .name = "Fundamental",
    .id = "fnd",
    .tab_width = 4,
    .lang_srv = NULL,
};

void languages_init(bool register_default) {
  if (register_default) {
    define_lang("C", "c", ".*\\.(c|h)", 2, "clangd");
    define_lang("C++", "cxx", ".*\\.(cpp|cxx|cc|c++|hh|h)", 2, "clangd");
    define_lang("Rust", "rs", ".*\\.rs", 4, "rust-analyzer");
    define_lang("Nix", "nix", ".*\\.nix", 4, "rnix-lsp");
    define_lang("Make", "make", ".*(Makefile|\\.mk)", 4, NULL);
    define_lang("Python", "python", ".*\\.py", 4, NULL);
  }
}

struct language lang_from_settings(const char *id) {
  struct language l;
  l.id = strdup(id);

  const char *key = setting_join_key("languages", id);

  // name
  const char *name_key = setting_join_key(key, "name");
  struct setting *name = settings_get(name_key);
  free((void *)name_key);
  l.name = name != NULL ? name->value.string_value : "Unknown";

  // tab width
  const char *tabw_key = setting_join_key(key, "tab-width");
  struct setting *tab_width = settings_get(tabw_key);
  free((void *)tabw_key);

  // fall back to global value
  if (tab_width == NULL) {
    tab_width = settings_get("editor.tab-width");
  }
  l.tab_width = tab_width != NULL ? tab_width->value.number_value : 4;

  // language server, TODO: move
  const char *langsrv_key = setting_join_key(key, "lang-srv");
  struct setting *lang_srv = settings_get(langsrv_key);
  free((void *)langsrv_key);
  l.lang_srv = lang_srv != NULL ? lang_srv->value.string_value : NULL;

  free((void *)key);
  return l;
}

void next_ext(const char *curr, const char **nxt, const char **end) {
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

struct setting *lang_setting(struct language *lang, const char *key) {
  const char *langkey = setting_join_key("languages", lang->id);
  const char *setting_key = setting_join_key(langkey, key);

  struct setting *res = settings_get(setting_key);

  free((void *)setting_key);
  free((void *)langkey);

  return res;
}

void lang_setting_set(struct language *lang, const char *key,
                      struct setting_value value) {
  const char *langkey = setting_join_key("languages", lang->id);
  const char *setting_key = setting_join_key(langkey, key);

  settings_set(setting_key, value);

  free((void *)setting_key);
  free((void *)langkey);
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
  free((void *)lang_path);
  free(settings);

  if (nsettings > 0) {
    return lang_from_settings(id);
  } else {
    minibuffer_echo_timeout(4, "failed to find language \"%s\"", id);
    return lang_from_settings("languages.fnd");
  }
}

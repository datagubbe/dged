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

  size_t offs = snprintf(namebuf, 128, "languages.%s.", id);

  char *b = namebuf + offs;
  snprintf(b, 128 - offs, "%s", "pattern");
  settings_register_setting(
      namebuf, (struct setting_value){.type = Setting_String,
                                      .string_value = (char *)pattern});

  snprintf(b, 128 - offs, "%s", "tab-width");
  settings_register_setting(namebuf,
                            (struct setting_value){.type = Setting_Number,
                                                   .number_value = tab_width});

  if (lang_srv != NULL) {
    snprintf(b, 128 - offs, "%s", "lang-srv");
    settings_register_setting(
        namebuf, (struct setting_value){.type = Setting_String,
                                        .string_value = (char *)lang_srv});
  }

  snprintf(b, 128 - offs, "%s", "name");
  settings_register_setting(
      namebuf, (struct setting_value){.type = Setting_String,
                                      .string_value = (char *)name});
}

static struct language g_fundamental = {
    .name = "Fundamental",
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

struct language lang_from_settings(const char *lang_path) {
  char setting_name_buf[128] = {0};
  size_t offs = snprintf(setting_name_buf, 128, "%s.", lang_path);
  char *b = setting_name_buf + offs;

  struct language l;

  snprintf(b, 128 - offs, "%s", "name");
  struct setting *name = settings_get(setting_name_buf);
  l.name = name != NULL ? name->value.string_value : "Unknown";

  snprintf(b, 128 - offs, "%s", "tab-width");
  struct setting *tab_width = settings_get(setting_name_buf);

  // fall back to global value
  if (tab_width == NULL) {
    tab_width = settings_get("editor.tab-width");
  }
  l.tab_width = tab_width != NULL ? tab_width->value.number_value : 4;

  snprintf(b, 128 - offs, "%s", "lang-srv");
  struct setting *lang_srv = settings_get(setting_name_buf);
  l.lang_srv = lang_srv != NULL ? lang_srv->value.string_value : NULL;

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

        char lang_path[128] = {0};
        strncpy(lang_path, setting->path, setting_name - setting->path);

        regfree(&regex);
        free(settings);
        return lang_from_settings(lang_path);
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

  char lang_path[128] = {0};
  snprintf(lang_path, 128, "languages.%s", id);

  // check that it exists
  struct setting **settings = NULL;
  uint32_t nsettings = 0;

  settings_get_prefix(lang_path, &settings, &nsettings);
  free(settings);

  if (nsettings > 0) {
    return lang_from_settings(lang_path);
  } else {
    minibuffer_echo_timeout(4, "failed to find language \"%s\"", id);
    return lang_from_settings("languages.fnd");
  }
}

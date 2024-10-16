#include <stdlib.h>

#include "dged/settings.h"

#include "assert.h"
#include "test.h"

void test_get(void) {
  settings_init(10);
  settings_set_default(
      "my.setting",
      (struct setting_value){.type = Setting_Bool, .data.bool_value = false});

  struct setting *s = settings_get("my.setting");
  ASSERT(s != NULL, "Expected setting to exist after being inserted");
  ASSERT(s->value.type == Setting_Bool,
         "Expected inserted setting to have the same type when retrieved");
  ASSERT(!s->value.data.bool_value,
         "Expected inserted setting to have the same value when retrieved");

  settings_set_default(
      "other.setting",
      (struct setting_value){.type = Setting_Number, .data.number_value = 28});

  struct setting **res = NULL;
  uint32_t nres = 0;
  settings_get_prefix("my", &res, &nres);

  ASSERT(nres == 1, "Expected to get one result back");
  ASSERT(s->value.type == Setting_Bool, "Expected inserted setting to have the "
                                        "same type when retrieved by prefix");
  ASSERT(!s->value.data.bool_value,
         "Expected inserted setting to have the same "
         "value when retrieved by prefix");

  free(res);

  settings_destroy();
}

void test_set(void) {
  settings_init(10);
  settings_set_default(
      "my.setting",
      (struct setting_value){.type = Setting_Bool, .data.bool_value = false});

  // test that wrong type is ignored;
  settings_set("my.setting",
               (struct setting_value){.type = Setting_String,
                                      .data.string_value = "bonan"});

  struct setting *s = settings_get("my.setting");
  ASSERT(s != NULL, "Expected setting to exist after being inserted");
  ASSERT(s->value.type == Setting_Bool,
         "Expected inserted setting type to not have been changed");
  ASSERT(!s->value.data.bool_value,
         "Expected inserted setting value to not have been changed");

  // test that correct type is indeed changed
  settings_set("my.setting", (struct setting_value){.type = Setting_Bool,
                                                    .data.bool_value = true});

  s = settings_get("my.setting");
  ASSERT(s != NULL, "Expected setting to exist");
  ASSERT(s->value.type == Setting_Bool,
         "Expected inserted setting type to not have been changed");
  ASSERT(s->value.data.bool_value,
         "Expected inserted setting value to _have_ been changed");

  settings_destroy();
}

void test_from_toml_string(void) {
  char *content = "[  languages.c]\n"
                  "name = \"C\"";

  settings_init(10);
  char **errmsgs = NULL;
  int32_t res = settings_from_string(content, &errmsgs);
  ASSERT(res == 0, "Expected valid TOML to parse successfully");

  struct setting *setting = settings_get("languages.c.name");
  ASSERT(setting != NULL,
         "Expected to be able to retrieve setting after parsed from string");
  ASSERT(setting->value.type == Setting_String, "Expected a string setting");
  ASSERT_STR_EQ(setting->value.data.string_value, "C",
                "Expected setting value to be \"C\"");

  content = "sune = \"wrong";
  res = settings_from_string(content, &errmsgs);
  ASSERT(res >= 1, "Expected (at least) one error from invalid toml");
  for (uint32_t i = 0; i < (uint32_t)res; ++i) {
    free(errmsgs[i]);
  }
  free(errmsgs);

  content = "boll = truj";
  res = settings_from_string(content, &errmsgs);
  ASSERT(res >= 1, "Expected (at least) one error from an invalid bool");
  for (uint32_t i = 0; i < (uint32_t)res; ++i) {
    free(errmsgs[i]);
  }
  free(errmsgs);

  content = "[editor]\n"
            "show-whitespace = true\n"
            "tab-width = 3\n";
  res = settings_from_string(content, &errmsgs);
  ASSERT(res == 0, "Expected valid TOML to parse successfully");

  setting = settings_get("editor.show-whitespace");
  ASSERT(setting != NULL,
         "Expected editor.show-whitespace to be set from TOML");
  ASSERT(setting->value.data.bool_value,
         "Expected editor.show-whitespace to be set to true from TOML");

  setting = settings_get("editor.tab-width");
  ASSERT(setting != NULL, "Expected editor.tab-width to be set from TOML");
  ASSERT(setting->value.data.number_value == 3,
         "Expected editor.tab-width to be set to 3 from TOML");

  content = "[languages]\n"
            "pang = { name = \"Bom\", \n"
            "description = \"Tjoff\" }\n";
  res = settings_from_string(content, &errmsgs);
  ASSERT(res == 0, "Expected valid TOML to parse successfully");

  setting = settings_get("languages.pang.name");
  ASSERT(setting != NULL,
         "Expected languages.pang.name to be set through inline table");
  ASSERT_STR_EQ(setting->value.data.string_value, "Bom",
                "Expected languages.pang.name to be \"Bom\"");

  content = "multi = \"\"\"This is\n"
            "a multiline string\"\"\"\n";
  res = settings_from_string(content, &errmsgs);
  ASSERT(res == 0, "Expected valid TOML to parse successfully");
  setting = settings_get("multi");
  ASSERT(setting != NULL, "Expected multi to be set");
  ASSERT_STR_EQ(setting->value.data.string_value, "This is\na multiline string",
                "Expected newline to have been preserved in multiline string");

  settings_destroy();
}

void run_settings_tests(void) {
  run_test(test_get);
  run_test(test_set);
  run_test(test_from_toml_string);
}

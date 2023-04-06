#include <stdlib.h>

#include "dged/settings.h"

#include "assert.h"
#include "test.h"

void test_get() {
  settings_init(10);
  settings_register_setting(
      "my.setting",
      (struct setting_value){.type = Setting_Bool, .bool_value = false});

  struct setting *s = settings_get("my.setting");
  ASSERT(s != NULL, "Expected setting to exist after being inserted");
  ASSERT(s->value.type == Setting_Bool,
         "Expected inserted setting to have the same type when retrieved");
  ASSERT(!s->value.bool_value,
         "Expected inserted setting to have the same value when retrieved");

  settings_register_setting(
      "other.setting",
      (struct setting_value){.type = Setting_Number, .number_value = 28});

  struct setting **res = NULL;
  uint32_t nres = 0;
  settings_get_prefix("my", &res, &nres);

  ASSERT(nres == 1, "Expected to get one result back");
  ASSERT(s->value.type == Setting_Bool, "Expected inserted setting to have the "
                                        "same type when retrieved by prefix");
  ASSERT(!s->value.bool_value, "Expected inserted setting to have the same "
                               "value when retrieved by prefix");

  free(res);

  settings_destroy();
}

void test_set() {
  settings_init(10);
  settings_register_setting(
      "my.setting",
      (struct setting_value){.type = Setting_Bool, .bool_value = false});

  // test that wrong type is ignored;
  settings_set("my.setting", (struct setting_value){.type = Setting_String,
                                                    .string_value = "bonan"});

  struct setting *s = settings_get("my.setting");
  ASSERT(s != NULL, "Expected setting to exist after being inserted");
  ASSERT(s->value.type == Setting_Bool,
         "Expected inserted setting type to not have been changed");
  ASSERT(!s->value.bool_value,
         "Expected inserted setting value to not have been changed");

  // test that correct type is indeed changed
  settings_set("my.setting", (struct setting_value){.type = Setting_Bool,
                                                    .bool_value = true});

  s = settings_get("my.setting");
  ASSERT(s != NULL, "Expected setting to exist");
  ASSERT(s->value.type == Setting_Bool,
         "Expected inserted setting type to not have been changed");
  ASSERT(s->value.bool_value,
         "Expected inserted setting value to _have_ been changed");

  settings_destroy();
}

void run_settings_tests() {
  run_test(test_get);
  run_test(test_set);
}

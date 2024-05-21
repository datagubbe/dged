#include "assert.h"
#include "test.h"

#include "dged/json.h"

#include <string.h>

void test_empty_parse(void) {
  struct json_result res = json_parse((uint8_t *)"", 0);

  ASSERT(res.ok, "Expected empty parse to work");
  json_destroy(&res.result.document);
}

void test_empty_array(void) {
  struct json_result res = json_parse((uint8_t *)"[]", 2);

  ASSERT(res.ok, "Expected parse of empty array to work");
  struct json_value root = res.result.document;
  ASSERT(root.type == Json_Array, "Expected doc root to be array");
  ASSERT(json_array_len(root.value.array) == 0, "Expected array to be empty");

  json_destroy(&root);
}

void test_array(void) {

  struct json_result res = json_parse((uint8_t *)"[ 1, 2, 4 ]", 11);
  ASSERT(res.ok, "Expected parse of number array to work");
  struct json_value root = res.result.document;
  ASSERT(root.type == Json_Array, "Expected doc root to be array");
  ASSERT(json_array_len(root.value.array) == 3, "Expected array len to be 3");

  json_destroy(&root);

  const char *jsn = "[ \"hello\", \"world\" ]";
  res = json_parse((uint8_t *)jsn, strlen(jsn));
  ASSERT(res.ok, "Expected parse of string array to work");
  root = res.result.document;
  ASSERT(root.type == Json_Array, "Expected doc root to be array");
  struct json_value *second = json_array_get(root.value.array, 1);
  ASSERT(second->type == Json_String, "Expected second element to be a string");
  ASSERT(s8cmp(second->value.string, s8("world")) == 0,
         "Expected second string to be \"world\"");

  json_destroy(&root);
}

void test_object(void) {
  struct json_result res = json_parse((uint8_t *)"{ }", 3);
  ASSERT(res.ok, "Expected parse of empty object to work");
  struct json_value root = res.result.document;
  ASSERT(root.type == Json_Object, "Expected doc root to be object");
  ASSERT(json_len(root.value.object) == 0, "Expected empty object len to be 0");

  json_destroy(&root);

  const char *jsn = "{ \"name\": \"Kalle Kula\", \"age\": 33, }";
  res = json_parse((uint8_t *)jsn, strlen(jsn));
  ASSERT(res.ok, "Expected parse of simple object to work");
  root = res.result.document;
  ASSERT(root.type == Json_Object, "Expected doc root to be object");
  ASSERT(json_contains(root.value.object, s8("name")),
         "Expected object to contain \"name\"");

  struct json_value *age = json_get(root.value.object, s8("age"));
  ASSERT(age->type == Json_Number, "Expected age to (just?) be a number");
  ASSERT(age->value.number == 33, "Expected age to be 33");

  json_destroy(&root);

  jsn = "{ \"name\": \"Kalle Kula\", \"age\": 33, \"kids\": "
        "[ "
        "{ \"name\": \"Sune Kula\", \"age\": 10, }, "
        "{ \"name\": \"Suna Kula\", \"age\": 7 } "
        "] }";
  res = json_parse((uint8_t *)jsn, strlen(jsn));
  ASSERT(res.ok, "Expected parse of nested object to work");
  root = res.result.document;
  ASSERT(root.type == Json_Object, "Expected doc root to be object");

  ASSERT(json_contains(root.value.object, s8("kids")),
         "Expected json object to contain array of kids");
  struct json_value *kids = json_get(root.value.object, s8("kids"));
  ASSERT(kids->type == Json_Array, "Expected kids to be array");

  json_destroy(&root);
}

void run_json_tests(void) {
  run_test(test_empty_parse);
  run_test(test_empty_array);
  run_test(test_array);
  run_test(test_object);
}

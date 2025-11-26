#include "assert.h"
#include "test.h"

#include "dged/json.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct parsed_json {
  struct json_result result;
  uint8_t *buf;
};

static struct parsed_json json_parse_error(const char *msg) {
  return (struct parsed_json){
      .result =
          (struct json_result){
              .ok = false,
              .result.error = msg,
          },
      .buf = NULL,
  };
}

static struct parsed_json parse_json_file(const char *path) {
  struct stat sb;
  if (stat(path, &sb) != 0) {
    return json_parse_error("file not found");
  }

  FILE *file = fopen(path, "r");
  if (fseek(file, 0, SEEK_END) != 0) {
    return json_parse_error("fseek to end failed");
  }

  long sz = ftell(file);
  if (sz == -1) {
    return json_parse_error("ftell failed");
  }

  rewind(file);

  uint8_t *buff = malloc(sz);
  int bytes = fread(buff, 1, sz, file);
  if (bytes != sz) {
    return json_parse_error("did not read whole file");
  }

  return (struct parsed_json){
      .result = json_parse(buff, sz),
      .buf = buff,
  };
}

static void test_empty_parse(void) {
  struct json_result res = json_parse((uint8_t *)"", 0);

  ASSERT(res.ok, "Expected empty parse to work");
  json_destroy(&res.result.document);
}

static void test_empty_array(void) {
  struct json_result res = json_parse((uint8_t *)"[]", 2);

  ASSERT(res.ok, "Expected parse of empty array to work");
  struct json_value root = res.result.document;
  ASSERT(root.type == Json_Array, "Expected doc root to be array");
  ASSERT(json_array_len(root.value.array) == 0, "Expected array to be empty");

  json_destroy(&root);
}

static void test_array(void) {

  struct json_result res = json_parse((uint8_t *)"[ 1, 2, 4 ]", 11);
  ASSERT(res.ok, "Expected parse of number array to work");
  struct json_value root = res.result.document;
  ASSERT(root.type == Json_Array, "Expected doc root to be array");
  ASSERT(json_array_len(root.value.array) == 3, "Expected array len to be 3");

  json_destroy(&root);

  const char *jsn = "[ \"hello\", \"world\", \"\\\"\" ]";
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

static void test_object(void) {
  struct json_result res = json_parse((uint8_t *)"{ }", 3);
  ASSERT(res.ok, "Expected parse of empty object to work");
  struct json_value root = res.result.document;
  ASSERT(root.type == Json_Object, "Expected doc root to be object");
  ASSERT(json_len(root.value.object) == 0, "Expected empty object len to be 0");

  json_destroy(&root);

  const char *jsn = "{ \"name\": \"Kalle Kula\", \"age\": 33, \"ball\": true, "
                    "\"square\": false }";
  res = json_parse((uint8_t *)jsn, strlen(jsn));
  ASSERT(res.ok, "Expected parse of simple object to work");
  root = res.result.document;
  ASSERT(root.type == Json_Object, "Expected doc root to be object");
  ASSERT(json_contains(root.value.object, s8("name")),
         "Expected object to contain \"name\"");

  struct json_value *age = json_get(root.value.object, s8("age"));
  ASSERT(age->type == Json_Number, "Expected age to (just?) be a number");
  ASSERT(age->value.number == 33, "Expected age to be 33");

  struct json_value *ball = json_get(root.value.object, s8("ball"));
  ASSERT(ball->type == Json_Bool, "Expected ball to be a boolean.");
  ASSERT(ball->value.boolean, "Expected Kalle Kulla to be a ball.");

  json_destroy(&root);

  jsn = "{ \"name\": \"Kalle Kula\", \"age\": 33, \"kids\": "
        "[ "
        "{ \"name\": \"Sune Kula\", \"age\": 10, }, "
        "{ \"name\": \"Suna Kula\", \"age\": 7 }, "
        "], \"pet_names\": [ \"fido\", \"fado\" ], \"ball\": true }";
  res = json_parse((uint8_t *)jsn, strlen(jsn));
  ASSERT(res.ok, "Expected parse of nested object to work");
  root = res.result.document;
  ASSERT(root.type == Json_Object, "Expected doc root to be object");

  ASSERT(json_contains(root.value.object, s8("kids")),
         "Expected json object to contain array of kids");
  struct json_value *kids = json_get(root.value.object, s8("kids"));
  ASSERT(kids->type == Json_Array, "Expected kids to be array");

  ball = json_get(root.value.object, s8("ball"));
  ASSERT(ball->type == Json_Bool, "Expected ball to be a boolean.");
  ASSERT(ball->value.boolean, "Expected Kalle Kulla to be a ball.");

  json_destroy(&root);
}

#define xstr(s) str(s)
#define str(s) #s

#define test_parse_file(path)                                                  \
  {                                                                            \
    struct parsed_json parsed = parse_json_file(xstr(TEST_ROOT) "/" path);     \
    ASSERT(parsed.result.ok, "Expected parsing of " #path " to work: %s",      \
           parsed.result.result.error);                                        \
    json_destroy(&parsed.result.result.document);                              \
    free(parsed.buf);                                                          \
  }

static void test_files(void) { test_parse_file("json/diag.json"); }

static void test_brackets_in_strings(void) {
  struct parsed_json parsed =
      parse_json_file(xstr(TEST_ROOT) "/json/diag2.json");
  ASSERT(parsed.result.ok, "Expected parsing of diag2.json to work");

  struct json_value *params =
      json_get(parsed.result.result.document.value.object, s8("params"));
  ASSERT(params != NULL, "Expected JSON object to contain params");
  ASSERT(params->type == Json_Object, "Expected params to be a JSON object");

  struct json_value *diagnostics =
      json_get(params->value.object, s8("diagnostics"));
  ASSERT(diagnostics != NULL, "Expected params to contain diagnostics");
  ASSERT(diagnostics->type == Json_Array, "Expected params to be a JSON array");

  struct json_array *diags = diagnostics->value.array;
  ASSERT(json_array_len(diags) == 5,
         "Expected diagnostics array to contain five items");

  json_destroy(&parsed.result.result.document);
  free(parsed.buf);
}

static void test_str_escape(void) {
  struct s8 res = unescape_json_string(s8("a\n\\n\r\\rb"));
  ASSERT(s8eq(res, s8("a\n\n\r\rb")), "Expected \\n and \\r to not be removed");

  s8delete(res);

  struct s8 res2 = unescape_json_string(s8("    \\\\\\n"));
  ASSERT(s8eq(res2, s8("    \\\n")), "Expected \\ and \\n to not be removed");
  s8delete(res2);

  struct s8 res3 = unescape_json_string(
      s8("\\n \\u26f0 \\u2702 \\u00a0\\u00a0\\u00a0\\u00a0"));
  ASSERT(s8eq(res3, s8("\n ⛰ ✂ \u00a0\u00a0\u00a0\u00a0")),
         "Expected unicode decoding to work.");
  s8delete(res3);
}

void run_json_tests(void) {
  run_test(test_empty_parse);
  run_test(test_empty_array);
  run_test(test_array);
  run_test(test_object);
  run_test(test_files);
  run_test(test_brackets_in_strings);
  run_test(test_str_escape);
}

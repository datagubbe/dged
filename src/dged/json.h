#ifndef _JSON_H
#define _JSON_H

#include <stdbool.h>
#include <stdint.h>

#include "s8.h"

enum json_type {
  Json_Array,
  Json_Object,
  Json_Null,
  Json_Number,
  Json_String,
  Json_Bool,
};

struct json_value {
  enum json_type type;
  union {
    struct s8 string;
    struct json_object *object;
    struct json_array *array;
    double number;
    bool boolean;
  } value;
};

struct json_result {
  bool ok;
  union {
    const char *error;
    struct json_value document;
  } result;
};

struct json_writer;

struct json_result json_parse(uint8_t *buf, uint64_t size);
void json_destroy(struct json_value *value);

uint64_t json_len(struct json_object *obj);
bool json_contains(struct json_object *obj, struct s8 key);
struct json_value *json_get(struct json_object *obj, struct s8 key);

uint64_t json_array_len(struct json_array *arr);
void json_array_foreach(struct json_array *arr,
                        void (*cb)(uint64_t, struct json_value));
struct json_value *json_array_get(struct json_array *arr, uint64_t idx);

struct json_writer *json_writer_create();
struct s8 json_writer_done(struct json_writer *writer);

#endif

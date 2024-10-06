#ifndef _JSON_H
#define _JSON_H

#include <stdbool.h>
#include <stdint.h>

#include "s8.h"

enum json_type {
  Json_Null = 0,
  Json_Array,
  Json_Object,
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
  struct json_value *parent;
};

struct json_result {
  bool ok;
  union {
    const char *error;
    struct json_value document;
  } result;
};

/**
 * Parse a json document from a string.
 *
 * @returns Structure describing the result of the parse
 * operation. The member @ref ok, if true represents a
 * successful parse, with the result in @ref result.document.
 * If @ref ok is false, the parse operation has an error,
 * and @ref result.error contains a descriptive error message.
 */
struct json_result json_parse(const uint8_t *buf, uint64_t size);

/**
 * Destroy a json value, returning all memory
 * allocated for the structure.
 *
 * @param [in] value The json value to destroy.
 */
void json_destroy(struct json_value *value);

/**
 * Check if a JSON object is empty.
 *
 * @param [in] obj The JSON object to check if empty.
 *
 * @returns True if @ref obj is empty, false otherwise.
 */
void json_empty(struct json_object *obj);

/**
 * Return the number of members in a JSON object.
 *
 * @param [in] obj The JSON object to get number of members for.
 *
 * @returns The number of members in @ref obj.
 */
uint64_t json_len(struct json_object *obj);

/**
 * Test if the JSON object contains the specified key.
 *
 * @param [in] obj The JSON object to look for @ref key in.
 * @param [in] key The key to search for.
 *
 * @returns True if @ref key exists in @ref obj, false otherwise.
 */
bool json_contains(struct json_object *obj, struct s8 key);

/**
 * Get a value from a JSON object.
 *
 * @param [in] obj The JSON object to get from.
 * @param [in] key The key of the value to get.
 *
 * @returns A pointer to the json value distinguished by @ref key,
 * if it exists, NULL otherwise.
 */
struct json_value *json_get(struct json_object *obj, struct s8 key);

/**
 * Get the length of a JSON array.
 *
 * @param [in] arr The array to get the length of
 *
 * @returns The length of @ref arr.
 */
uint64_t json_array_len(struct json_array *arr);

/**
 * Iterate a JSON array.
 *
 * @param [in] arr The array to iterate.
 * @param [in] cb The callback to invoke for each member in @ref arr.
 */
void json_array_foreach(struct json_array *arr,
                        void (*cb)(uint64_t, struct json_value));

/**
 * Get a member from a JSON array by index.
 *
 * @param [in] arr The array to get from.
 * @param [in] idx The index to get the value at.
 *
 * @returns A pointer to the value at @ref idx in @ref arr. If @ref idx
 * is outside the array length, this returns NULL.
 */
struct json_value *json_array_get(struct json_array *arr, uint64_t idx);

/**
 * Render a JSON value to a string.
 *
 * @param [in] val The json value to render to a string.
 *
 * @returns The JSON object rendered as a string.
 */
struct s8 json_value_to_string(const struct json_value *val);

#endif

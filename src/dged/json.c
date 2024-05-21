#include "json.h"

#include "hash.h"
#include "hashmap.h"
#include "utf8.h"
#include "vec.h"

#include <stddef.h>
#include <stdio.h>

HASHMAP_ENTRY_TYPE(json_object_member, struct json_value);

struct json_object {
  HASHMAP(struct json_object_member) members;
};

struct json_array {
  VEC(struct json_value) values;
};

static void setarray(struct json_value *val) {
  val->type = Json_Array;
  val->value.array = calloc(1, sizeof(struct json_array));
  VEC_INIT(&val->value.array->values, 10);
}

static void setobject(struct json_value *val) {
  val->type = Json_Object;
  val->value.object = calloc(1, sizeof(struct json_object));
  HASHMAP_INIT(&val->value.object->members, 10, hash_name);
}

static void setstring(struct json_value *val, uint8_t *current) {
  val->type = Json_String;
  val->value.string.s = current;
  val->value.string.l = 0;
}

static bool is_number(uint8_t byte) { return byte >= '0' && byte <= '9'; }

enum object_parse_state {
  ObjectParseState_Key,
  ObjectParseState_Value,
};

struct json_result json_parse(uint8_t *buf, uint64_t size) {
  struct json_result res = {
      .ok = true,
      .result.document.type = Json_Null,
  };

  struct json_value *parent = NULL;
  struct json_value *current = &res.result.document;
  struct json_value tmp_key = {0};
  struct json_value tmp_val = {0};
  uint32_t line = 1, col = 0;

  enum object_parse_state obj_parse_state = ObjectParseState_Key;
  for (uint64_t bufi = 0; bufi < size; ++bufi) {
    uint8_t byte = buf[bufi];

    // handle appends to the current scope
    if (current->type == Json_Array) {
      VEC_PUSH(&current->value.array->values, tmp_val);
      parent = current;

      // start looking for next value
      tmp_val.type = Json_Null;
      current = &tmp_val;
    } else if (current->type == Json_Object &&
               obj_parse_state == ObjectParseState_Key) {
      // key is in tmp_key, start looking for value
      obj_parse_state = ObjectParseState_Value;
      parent = current;

      tmp_val.type = Json_Null;
      current = &tmp_val;
    } else if (current->type == Json_Object &&
               obj_parse_state == ObjectParseState_Value) {
      // value is in tmp_val
      // TODO: remove this alloc, should not be needed
      char *k = s8tocstr(tmp_key.value.string);
      uint32_t hash = 0;
      HASHMAP_INSERT(&current->value.object->members, struct json_object_member,
                     k, tmp_val, hash);
      (void)hash;
      free(k);

      // start looking for next key
      obj_parse_state = ObjectParseState_Key;
      parent = current;

      tmp_key.type = Json_Null;
      current = &tmp_key;
    }

    switch (byte) {
    case '[':
      setarray(current);
      parent = current;

      tmp_val.type = Json_Null;
      current = &tmp_val;
      break;
    case ']':
      current = parent;
      break;
    case '{':
      setobject(current);
      obj_parse_state = ObjectParseState_Key;
      parent = current;

      tmp_key.type = Json_Null;
      current = &tmp_key;
      break;
    case '}':
      current = parent;
      break;
    case '"':
      if (current->type == Json_String) {
        // finish off the string
        current->value.string.l = (buf + bufi) - current->value.string.s;
        current = parent;
      } else {
        setstring(current, buf + bufi + 1 /* skip " */);
      }
      break;
    case '\n':
      ++line;
      col = 0;
      break;
    default:
      if (current->type == Json_String) {
        // append to string
      } else if (current->type == Json_Number &&
                 !(is_number(byte) || byte == '-' || byte == '.')) {
        // end of number
        current->value.string.l = (buf + bufi) - current->value.string.s;
        char *nmbr = s8tocstr(current->value.string);
        current->value.number = atof(nmbr);
        free(nmbr);

        current = parent;

      } else if (current->type == Json_Null &&
                 (is_number(byte) || byte == '-' || byte == '.')) {
        // borrow string storage in the value for storing number
        // as a string
        setstring(current, buf + bufi);
        current->type = Json_Number;
      } else if (byte == 't') {
        current->type = Json_Bool;
        current->value.boolean = true;

        current = parent;
      } else if (byte == 'f') {
        current->type = Json_Bool;
        current->value.boolean = false;

        current = parent;
      } else if (byte == 'n') {
        current->type = Json_Null;

        current = parent;
      }
      break;
    }

    // TODO: not entirely correct
    ++col;
  }
  return res;
}

void json_destroy(struct json_value *value) {
  switch (value->type) {
  case Json_Array:
    struct json_array *arr = value->value.array;
    VEC_FOR_EACH(&arr->values, struct json_value * val) { json_destroy(val); }
    VEC_DESTROY(&arr->values);
    break;
  case Json_Object:
    struct json_object *obj = value->value.object;
    HASHMAP_FOR_EACH(&obj->members, struct json_object_member * memb) {
      json_destroy(&memb->value);
    }

    HASHMAP_DESTROY(&obj->members);
  case Json_Null:
  case Json_Number:
  case Json_String:
  case Json_Bool:
    break;
  }
}

uint64_t json_array_len(struct json_array *arr) {
  return VEC_SIZE(&arr->values);
}

struct json_value *json_array_get(struct json_array *arr, uint64_t idx) {
  struct json_value *ret = NULL;

  if (idx <= VEC_SIZE(&arr->values)) {
    ret = &VEC_ENTRIES(&arr->values)[idx];
  }

  return ret;
}

uint64_t json_len(struct json_object *obj) {
  return HASHMAP_SIZE(&obj->members);
}

bool json_contains(struct json_object *obj, struct s8 key) {
  // TODO: get rid of alloc
  char *k = s8tocstr(key);
  HASHMAP_CONTAINS_KEY(&obj->members, struct json_object_member, k, bool res);

  free(k);

  return res;
}

struct json_value *json_get(struct json_object *obj, struct s8 key) {
  // TODO: get rid of alloc
  char *k = s8tocstr(key);
  HASHMAP_GET(&obj->members, struct json_object_member, k,
              struct json_value * result);

  free(k);

  return result;
}

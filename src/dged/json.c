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

static struct json_value create_array(struct json_value *parent) {
  struct json_value val = {0};
  val.type = Json_Array;
  val.parent = parent;
  val.value.array = calloc(1, sizeof(struct json_array));
  VEC_INIT(&val.value.array->values, 10);

  return val;
}

static struct json_value create_object(struct json_value *parent) {
  struct json_value val = {0};
  val.type = Json_Object;
  val.parent = parent;
  val.value.object = calloc(1, sizeof(struct json_object));
  HASHMAP_INIT(&val.value.object->members, 10, hash_name);

  return val;
}

static struct json_value create_string(const uint8_t *start, uint32_t len,
                                       struct json_value *parent) {
  struct json_value val = {0};
  val.type = Json_String;
  val.parent = parent;
  val.value.string.s = (uint8_t *)start;
  val.value.string.l = len;

  return val;
}

static bool is_number(uint8_t byte) { return byte >= '0' && byte <= '9'; }

enum object_parse_state {
  ObjectParseState_Key,
  ObjectParseState_Value,
};

struct parser_state {
  const uint8_t *buf;
  uint64_t pos;
  uint64_t len;
  uint32_t line;
  uint32_t col;
};

static struct json_result parse_string(struct parser_state *state,
                                       struct json_value *parent) {
  uint64_t start_pos = ++state->pos; /* ++ to skip start of string (") */
  while (state->pos < state->len && state->buf[state->pos] != '"') {
    ++state->pos;
    ++state->col;
  }

  if (state->pos < state->len) {
    uint64_t len = state->pos - start_pos;

    // skip over "
    ++state->pos;
    ++state->col;

    return (struct json_result){
        .ok = true,
        .result.document = create_string(&state->buf[start_pos], len, parent),
    };
  }

  return (struct json_result){
      .ok = false,
      .result.error = "expected end of string, found EOF",
  };
}

static struct json_result parse_number(struct parser_state *state,
                                       struct json_value *parent) {
  uint64_t start_pos = state->pos;
  while (state->pos < state->len &&
         (is_number(state->buf[state->pos]) || state->buf[state->pos] == '-' ||
          state->buf[state->pos] == '.')) {
    ++state->pos;
    ++state->col;
  }

  if (state->pos < state->len) {
    uint64_t len = state->pos - start_pos;
    ++state->pos;
    ++state->col;
    char *nmbr =
        s8tocstr((struct s8){.s = (uint8_t *)&state->buf[start_pos], .l = len});
    struct json_result res = {
        .ok = true,
        .result.document.type = Json_Number,
        .result.document.value.number = atof(nmbr),
        .result.document.parent = parent,
    };
    free(nmbr);
    return res;
  }

  return (struct json_result){
      .ok = false,
      .result.error = "expected end of number, found EOF",
  };
}

static struct json_result parse_value(struct parser_state *state,
                                      struct json_value *parent) {
  uint8_t byte = state->buf[state->pos];
  switch (byte) {
  case '"':
    return parse_string(state, parent);
  case 't':
    state->pos += 4;
    state->col += 4;
    return (struct json_result){
        .ok = true,
        .result.document.type = Json_Bool,
        .result.document.value.boolean = true,
        .result.document.parent = parent,
    };
  case 'f':
    state->pos += 5;
    state->col += 5;
    return (struct json_result){
        .ok = true,
        .result.document.type = Json_Bool,
        .result.document.value.boolean = false,
        .result.document.parent = parent,
    };
  case 'n':
    state->pos += 4;
    state->col += 4;
    return (struct json_result){
        .ok = true,
        .result.document.type = Json_Null,
        .result.document.parent = parent,
    };
  default:
    if (is_number(byte) || byte == '-' || byte == '.') {
      return parse_number(state, parent);
    }
    break;
  }

  return (struct json_result){
      .ok = false,
      .result.error = "expected value",
  };
}

struct json_result json_parse(const uint8_t *buf, uint64_t size) {

  enum object_parse_state expected = ObjectParseState_Value;
  struct parser_state state = {
      .buf = buf,
      .pos = 0,
      .len = size,
      .line = 1,
      .col = 0,
  };

  struct json_value root = {0}, key = {0}, value = {0};
  struct json_value *container = &root;

  while (state.pos < state.len) {
    switch (state.buf[state.pos]) {
    case ',':
    case ' ':
    case ':':
    case '\r':
    case '\t':
      ++state.col;
      ++state.pos;
      continue;

    case '\n':
      ++state.line;
      ++state.pos;
      state.col = 0;
      continue;

    case ']':
    case '}':
      container = container->parent;
      ++state.pos;
      ++state.col;
      continue;

    case '[':
      value = create_array(container);
      ++state.pos;
      ++state.col;
      break;
    case '{':
      value = create_object(container);
      expected = ObjectParseState_Key;
      ++state.pos;
      ++state.col;
      break;
    default:
      if (expected == ObjectParseState_Key) {
        struct json_result res = parse_string(&state, container);

        if (!res.ok) {
          return res;
        }

        key = res.result.document;
        expected = ObjectParseState_Value;

        // dont insert anything now, we still need a value
        continue;
      } else {
        struct json_result res = parse_value(&state, container);

        if (!res.ok) {
          return res;
        }

        value = res.result.document;
      }
    }

    struct json_value *inserted = NULL;
    // where to put value?
    if (container->type == Json_Object) {
      // TODO: remove this alloc, should not be needed
      char *k = s8tocstr(key.value.string);
      HASHMAP_APPEND(&container->value.object->members,
                     struct json_object_member, k,
                     struct json_object_member * val);

      // TODO: duplicate key
      if (val != NULL) {
        inserted = &val->value;
        val->value = value;
        // start looking for next key
        expected = ObjectParseState_Key;
      }

      free(k);
    } else if (container->type == Json_Array) {
      VEC_APPEND(&container->value.array->values, struct json_value * val);
      inserted = val;
      *val = value;
    } else { // root
      *container = value;
      inserted = container;
    }

    // did we insert a container?
    // In this case, this is the current container
    if (inserted != NULL &&
        (value.type == Json_Object || value.type == Json_Array)) {
      container = inserted;
    }
  }

  return (struct json_result){
      .ok = true,
      .result.document = root,
  };
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

uint64_t json_empty(struct json_object *obj) {
  return json_len(obj) == 0;
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

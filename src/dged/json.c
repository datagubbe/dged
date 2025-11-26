#include "json.h"

#include "hash.h"
#include "hashmap.h"
#include "utf8.h"
#include "vec.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

struct json_key_value {
  struct s8 key;
  struct json_value value;
};

HASHMAP_ENTRY_TYPE(json_object_member, struct json_key_value);

static char errbuf[1024] = {0};

static const char *format_error(uint32_t line, uint32_t col, const char *msg) {
  snprintf(errbuf, 1024, "(%d, %d): %s", line, col, msg);
  return errbuf;
}

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

static uint32_t codepoint_from_hex(uint8_t bytes[4]) {
  uint32_t nmbr = 0;
  for (size_t i = 0; i < 4; ++i) {
    uint8_t byte = bytes[i];
    uint32_t value = 0;
    if (byte >= '0' && byte <= '9') {
      value = byte - '0';
    } else if (byte >= 'A' && byte <= 'F') {
      value = byte - 'A' + 10;
    } else if (byte >= 'a' && byte <= 'f') {
      value = byte - 'a' + 10;
    }

    // 16 ^ (3-i)
    uint32_t multiplier = 1 << (4 * (3 - i));
    nmbr += value * multiplier;
  }

  return nmbr;
}

struct s8 unescape_json_string(struct s8 input) {
  size_t new_size = 0;
  bool escape = false;
  for (size_t bi = 0; bi < input.l; ++bi) {
    uint8_t b = input.s[bi];

    size_t sz = 1;
    if (b == '\\' && !escape) {
      escape = true;
      continue;
    }

    if (b == 'u' && escape) {
      // unicode codepoint, calculate byte-width
      // format is \uXXXX where X is a hex digit.
      uint8_t chars[4];
      uint32_t codepoint = codepoint_from_hex(&input.s[bi + 1]);
      sz = utf8_encode(codepoint, chars);
      bi += 4;
    }

    new_size += sz;
    escape = false;
  }

  escape = false;
  uint8_t *buf = calloc(new_size, 1);
  size_t bufi = 0;
  for (size_t bi = 0; bi < input.l; ++bi) {
    uint8_t b = input.s[bi];

    if (b == '\\' && !escape) {
      escape = true;
      continue;
    }

    size_t skip = 1;
    if (escape) {
      switch (b) {
      case 'b':
        buf[bufi] = '\b';
        break;
      case '\\':
        buf[bufi] = '\\';
        break;
      case 'f':
        buf[bufi] = '\f';
        break;
      case 'n':
        buf[bufi] = '\n';
        break;
      case 'r':
        buf[bufi] = '\r';
        break;
      case 't':
        buf[bufi] = '\t';
        break;
      case 'u': {
        uint8_t chars[4] = {0};
        uint32_t codepoint = codepoint_from_hex(&input.s[bi + 1]);
        size_t size = utf8_encode(codepoint, chars);
        memcpy(&buf[bufi], chars, size);
        skip = size;
        bi += 4;
      } break;
      case '"':
        buf[bufi] = '"';
        break;
      default:
        buf[bufi] = b;
      }
    } else {
      buf[bufi] = b;
    }

    escape = false;
    bufi += skip;
  }

  return (struct s8){
      .s = buf,
      .l = new_size,
  };
}

struct s8 escape_json_string(struct s8 input) {
  size_t new_size = 0;
  for (size_t bi = 0; bi < input.l; ++bi) {
    uint8_t b = input.s[bi];
    switch (b) {
    case '\\':
    case '\b':
    case '\f':
    case '\n':
    case '\r':
    case '\t':
    case '"':
      new_size += 2;
      break;
    default:
      ++new_size;
    }
  }

  uint8_t *buf = calloc(new_size, 1);
  size_t bufi = 0;
  for (size_t bi = 0; bi < input.l; ++bi) {
    uint8_t b = input.s[bi];
    switch (b) {
    case '\\':
      buf[bufi] = '\\';
      buf[bufi + 1] = '\\';
      bufi += 2;
      break;
    case '\b':
      buf[bufi] = '\\';
      buf[bufi + 1] = 'b';
      bufi += 2;
      break;
    case '\f':
      buf[bufi] = '\\';
      buf[bufi + 1] = 'f';
      bufi += 2;
      break;
    case '\n':
      buf[bufi] = '\\';
      buf[bufi + 1] = 'n';
      bufi += 2;
      break;
    case '\r':
      buf[bufi] = '\\';
      buf[bufi + 1] = 'r';
      bufi += 2;
      break;
    case '\t':
      buf[bufi] = '\\';
      buf[bufi + 1] = 't';
      bufi += 2;
      break;
    case '"':
      buf[bufi] = '\\';
      buf[bufi + 1] = '"';
      bufi += 2;
      break;
    default:
      buf[bufi] = b;
      ++bufi;
    }
  }

  return (struct s8){
      .s = buf,
      .l = new_size,
  };
}

static struct json_value create_string(const uint8_t *start, uint32_t len,
                                       struct json_value *parent) {
  struct json_value val = {0};
  val.type = Json_String;
  val.parent = parent;
  val.value.string.s = (uint8_t *)start;
  val.value.string.l = len;
  val.start = start;
  val.end = start + len;

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
  bool literal = false;
  while (state->pos < state->len &&
         (literal || state->buf[state->pos] != '"')) {

    // skip literal " escaped with \"
    literal = state->buf[state->pos] == '\\';
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
    char *nmbr =
        s8tocstr((struct s8){.s = (uint8_t *)&state->buf[start_pos], .l = len});
    struct json_result res = {
        .ok = true,
        .result.document.type = Json_Number,
        .result.document.value.number = atof(nmbr),
        .result.document.parent = parent,
        .result.document.start = &state->buf[start_pos],
        .result.document.end = &state->buf[state->pos],
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
        .result.document.start = &state->buf[state->pos - 4],
        .result.document.end = &state->buf[state->pos],
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
        .result.document.start = &state->buf[state->pos - 5],
        .result.document.end = &state->buf[state->pos],
        .result.document.parent = parent,
    };
  case 'n':
    state->pos += 4;
    state->col += 4;
    return (struct json_result){
        .ok = true,
        .result.document.type = Json_Null,
        .result.document.start = &state->buf[state->pos - 4],
        .result.document.end = &state->buf[state->pos],
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
      .result.error = format_error(state->line, state->col, "expected value"),
  };
}

struct json_value *insert(struct json_value *container, struct json_value *key_,
                          struct json_value *value) {

  struct json_value *inserted = NULL;
  // where to put value?
  if (container->type == Json_Object) {
    // TODO: remove this alloc, should not be needed
    char *k = s8tocstr(key_->value.string);
    HASHMAP_APPEND(&container->value.object->members, struct json_object_member,
                   k, struct json_object_member * val);

    // TODO: duplicate key
    if (val != NULL) {
      inserted = &val->value.value;
      val->value.value = *value;
      val->value.key = s8dup(key_->value.string);
    }

    free(k);
  } else if (container->type == Json_Array) {
    VEC_APPEND(&container->value.array->values, struct json_value * val);
    inserted = val;
    *val = *value;
  } else { // root
    *container = *value;
    inserted = container;
  }

  return inserted;
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
      container->end = &state.buf[state.pos + 1];
      container = container->parent;

      if (container->type == Json_Object) {
        expected = ObjectParseState_Key;
      }

      ++state.pos;
      ++state.col;
      continue;

    case '[':
      value = create_array(container);
      value.start = &state.buf[state.pos];
      ++state.pos;
      ++state.col;
      break;
    case '{':
      value = create_object(container);
      value.start = &state.buf[state.pos];
      ++state.pos;
      ++state.col;
      break;
    default:
      // parse out a value or a key
      switch (expected) {

      case ObjectParseState_Key: {
        if (container->type == Json_Object) {
          struct json_result res = parse_string(&state, container);

          if (!res.ok) {
            json_destroy(&root);
            return res;
          }

          key = res.result.document;
        }
        expected = ObjectParseState_Value;
        // dont insert anything now, we still need a value
        continue;
      }

      case ObjectParseState_Value: {
        struct json_result res = parse_value(&state, container);

        if (!res.ok) {
          json_destroy(&root);
          return res;
        }

        value = res.result.document;

        if (container->type == Json_Object) {
          expected = ObjectParseState_Key;
        }
        break;
      }
      }
      break;
    }

    // insert the value we have created into the
    // structure
    struct json_value *inserted = insert(container, &key, &value);

    // did we insert a container?
    // In this case, this is the current container and
    // set the expectation for value or key correctly
    // depending on the type
    if (inserted != NULL &&
        (value.type == Json_Object || value.type == Json_Array)) {
      container = inserted;

      if (value.type == Json_Object) {
        expected = ObjectParseState_Key;
      } else {
        expected = ObjectParseState_Value;
      }
    }
  }

  return (struct json_result){
      .ok = true,
      .result.document = root,
  };
}

void json_destroy(struct json_value *value) {
  switch (value->type) {
  case Json_Array: {
    struct json_array *arr = value->value.array;
    VEC_FOR_EACH(&arr->values, struct json_value * val) { json_destroy(val); }
    VEC_DESTROY(&arr->values);
    free(arr);
  } break;
  case Json_Object: {
    struct json_object *obj = value->value.object;
    HASHMAP_FOR_EACH(&obj->members, struct json_object_member * memb) {
      s8delete(memb->value.key);
      json_destroy(&memb->value.value);
    }

    HASHMAP_DESTROY(&obj->members);
    free(obj);
  } break;
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

bool json_empty(struct json_object *obj) { return json_len(obj) == 0; }

bool json_contains(struct json_object *obj, struct s8 key) {
  // TODO: get rid of alloc
  char *k = s8tocstr(key);
  HASHMAP_CONTAINS_KEY(&obj->members, struct json_object_member, k, bool res);

  free(k);

  return res;
}

void json_foreach(struct json_object *obj,
                  void (*cb)(struct s8, struct json_value *, void *),
                  void *userdata) {
  HASHMAP_FOR_EACH(&obj->members, struct json_object_member * entry) {
    cb(entry->value.key, &entry->value.value, userdata);
  }
}

struct json_value *json_get(struct json_object *obj, struct s8 key) {
  // TODO: get rid of alloc
  char *k = s8tocstr(key);
  HASHMAP_GET(&obj->members, struct json_object_member, k,
              struct json_key_value * result);

  free(k);

  return result != NULL ? &result->value : NULL;
}

void json_set(struct json_object *obj, struct s8 key_, struct json_value val) {
  // TODO: get rid of alloc
  char *k = s8tocstr(key_);
  uint32_t hash = 0;

  struct json_key_value v = {
      .value = val,
      .key = s8dup(key_),
  };
  HASHMAP_INSERT(&obj->members, struct json_object_member, k, v, hash);

  (void)hash;
  (void)key;
  free(k);
}

void json_array_foreach(struct json_array *arr, void *userdata,
                        void (*cb)(uint64_t, struct json_value *, void *)) {

  VEC_FOR_EACH_INDEXED(&arr->values, struct json_value * val, i) {
    cb(i, val, userdata);
  }
}

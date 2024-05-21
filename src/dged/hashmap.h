#ifndef _HASHMAP_H
#define _HASHMAP_H

#include "vec.h"
#include <stdint.h>

#define HASHMAP_ENTRY_TYPE(name, entry)                                        \
  struct name {                                                                \
    uint32_t key;                                                              \
    entry value;                                                               \
  }

#define HASHMAP(entry)                                                         \
  struct {                                                                     \
    VEC(entry) entries;                                                        \
    uint32_t (*hash_fn)(const char *);                                         \
  }

#define HASHMAP_INIT(map, initial_capacity, hasher)                            \
  VEC_INIT(&(map)->entries, initial_capacity)                                  \
  (map)->hash_fn = hasher;

#define HASHMAP_DESTROY(map) VEC_DESTROY(&(map)->entries)

#define HASHMAP_INSERT(map, type, k, v, hash_var)                              \
  uint32_t key = (map)->hash_fn(k);                                            \
  bool duplicate = false;                                                      \
  VEC_FOR_EACH(&(map)->entries, type *pair) {                                  \
    if (pair->key == key) {                                                    \
      duplicate = true;                                                        \
      break;                                                                   \
    }                                                                          \
  }                                                                            \
  if (!duplicate) {                                                            \
    VEC_PUSH(&(map)->entries, ((type){.key = key, .value = v}));               \
  }                                                                            \
  hash_var = key;

#define HASHMAP_APPEND(map, type, k, var)                                      \
  uint32_t key = (map)->hash_fn(k);                                            \
  bool duplicate = false;                                                      \
  VEC_FOR_EACH(&(map)->entries, type *pair) {                                  \
    if (pair->key == key) {                                                    \
      duplicate = true;                                                        \
      break;                                                                   \
    }                                                                          \
  }                                                                            \
  type *v = NULL;                                                              \
  if (!duplicate) {                                                            \
    VEC_APPEND(&(map)->entries, v);                                            \
    v->key = key;                                                              \
  }                                                                            \
  var = v;

#define HASHMAP_GET(map, type, k, var)                                         \
  HASHMAP_GET_BY_HASH(map, type, (map)->hash_fn(k), var)

#define HASHMAP_GET_BY_HASH(map, type, h, var)                                 \
  type *res = NULL;                                                            \
  uint32_t needle = h;                                                         \
  VEC_FOR_EACH(&(map)->entries, type *pair) {                                  \
    if (needle == pair->key) {                                                 \
      res = pair;                                                              \
      break;                                                                   \
    }                                                                          \
  }                                                                            \
  var = res != NULL ? &(res->value) : NULL;

#define HASHMAP_CONTAINS_KEY(map, type, k, var)                                \
  uint32_t needle = (map)->hash_fn(k);                                         \
  bool exists = false;                                                         \
  VEC_FOR_EACH(&(map)->entries, type *pair) {                                  \
    if (needle == pair->key) {                                                 \
      exists = true;                                                           \
      break;                                                                   \
    }                                                                          \
  }                                                                            \
  var = exists;

#define HASHMAP_FOR_EACH(map, var) VEC_FOR_EACH_INDEXED(&(map)->entries, var, i)

#define HASHMAP_SIZE(map) VEC_SIZE(&(map)->entries)
#define HASHMAP_CAPACITY(map) VEC_CAPACITY(&(map)->entries)
#define HASHMAP_EMPTY(map) HASHMAP_SIZE == 0

#endif

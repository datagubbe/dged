#ifndef _VEC_H
#define _VEC_H

#include <stdlib.h>

#define VEC(entry)                                                             \
  struct {                                                                     \
    entry *temp;                                                               \
    entry *entries;                                                            \
    uint32_t nentries;                                                         \
    uint32_t capacity;                                                         \
  }

#define VEC_INIT(vec, initial_capacity)                                        \
  (vec)->entries = malloc(sizeof((vec)->entries[0]) * initial_capacity);       \
  (vec)->temp = calloc(1, sizeof((vec)->entries[0]));                          \
  (vec)->capacity = initial_capacity;                                          \
  (vec)->nentries = 0;

#define VEC_DISOWN_ENTRIES(vec) (vec)->entries = NULL;

#define VEC_DESTROY(vec)                                                       \
  free((vec)->temp);                                                           \
  free((vec)->entries);                                                        \
  (vec)->entries = NULL;                                                       \
  (vec)->capacity = 0;                                                         \
  (vec)->nentries = 0;

#define VEC_GROW(vec, new_size)                                                \
  if (new_size > (vec)->capacity) {                                            \
    (vec)->capacity = new_size;                                                \
    (vec)->entries = realloc((vec)->entries,                                   \
                             (sizeof((vec)->entries[0]) * (vec)->capacity));   \
  }

#define VEC_PUSH(vec, entry)                                                   \
  if ((vec)->nentries + 1 >= (vec)->capacity) {                                \
    VEC_GROW(vec, (vec)->capacity * 2);                                        \
  }                                                                            \
                                                                               \
  (vec)->entries[(vec)->nentries] = entry;                                     \
  ++(vec)->nentries;

#define VEC_POP(vec, var)                                                      \
  var = (vec)->entries[(vec)->nentries - 1];                                   \
  if ((vec)->nentries > 0) {                                                   \
    --(vec)->nentries;                                                         \
  }

#define VEC_SWAP(vec, idx1, idx2)                                              \
  if (idx1 < (vec)->nentries && idx2 < (vec)->nentries && idx1 >= 0 &&         \
      idx2 >= 0) {                                                             \
    *((vec)->temp) = (vec)->entries[idx1];                                     \
    (vec)->entries[idx1] = (vec)->entries[idx2];                               \
    (vec)->entries[idx2] = *((vec)->temp);                                     \
  }

#define VEC_APPEND(vec, var)                                                   \
  if ((vec)->nentries + 1 >= (vec)->capacity) {                                \
    VEC_GROW(vec, (vec)->capacity * 2);                                        \
  }                                                                            \
                                                                               \
  var = &((vec)->entries[(vec)->nentries]);                                    \
  ++(vec)->nentries;

#define VEC_FOR_EACH(vec, var) VEC_FOR_EACH_INDEXED(vec, var, i)

#define VEC_FOR_EACH_INDEXED(vec, var, idx)                                    \
  for (uint32_t keep = 1, idx = 0, size = (vec)->nentries;                     \
       keep && idx != size; keep = !keep, idx++)                               \
    for (var = (vec)->entries + idx; keep; keep = !keep)

#define VEC_SIZE(vec) (vec)->nentries
#define VEC_CAPACITY(vec) (vec)->capacity
#define VEC_ENTRIES(vec) (vec)->entries
#define VEC_EMPTY(vec) ((vec)->nentries == 0)

#define VEC_CLEAR(vec) (vec)->nentries = 0
#define VEC_FRONT(vec) ((vec)->nentries > 0 ? &((vec)->entries[0]) : NULL)
#define VEC_BACK(vec)                                                          \
  ((vec)->nentries > 0 ? &((vec)->entries[(vec)->nentries - 1]) : NULL)

#endif

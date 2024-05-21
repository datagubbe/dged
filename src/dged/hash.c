#include "hash.h"

uint32_t hash_name(const char *s) {
  unsigned long hash = 5381;
  int c;

  while ((c = *s++))
    hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

  return hash;
}

uint32_t hash_name_s8(struct s8 s) {
  unsigned long hash = 5381;

  for (uint64_t i = 0; i < s.l; ++i)
    hash = ((hash << 5) + hash) + s.s[i]; /* hash * 33 + c */

  return hash;
}

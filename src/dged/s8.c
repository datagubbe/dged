#include "s8.h"

#include <stdlib.h>
#include <string.h>

bool s8eq(struct s8 s1, struct s8 s2) {
  return s1.l == s2.l && memcmp(s1.s, s2.s, s1.l) == 0;
}

int s8cmp(struct s8 s1, struct s8 s2) {
  if (s1.l < s2.l) {
    return memcmp(s1.s, s2.s, s1.l);
  } else if (s2.l < s1.l) {
    return memcmp(s1.s, s2.s, s2.l);
  }

  return memcmp(s1.s, s2.s, s1.l);
}

char *s8tocstr(struct s8 s) {
  char *cstr = (char *)malloc(s.l + 1);
  memcpy(cstr, s.s, s.l);
  cstr[s.l] = '\0';
  return cstr;
}

bool s8startswith(struct s8 s, struct s8 prefix) {
  if (prefix.l > s.l) {
    return false;
  }

  return memcmp(s.s, prefix.s, prefix.l) == 0;
}

struct s8 s8dup(struct s8 s) {
  struct s8 new = {0};
  new.l = s.l;

  new.s = (char *)malloc(s.l);
  memcpy(new.s, s.s, s.l);

  return new;
}

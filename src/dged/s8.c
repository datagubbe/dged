#include "s8.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct s8 s8new(const char *s, size_t len) {
  uint8_t *mem = calloc(len + 1, 1);
  memcpy(mem, s, len);
  return (struct s8){
      .s = mem,
      .l = len,
  };
}

struct s8 s8substr(struct s8 s, size_t start, size_t end) {
  end = end > start ? end : start;
  start = start < end ? start : end;
  size_t len = end - start;

  if (len == 0) {
    return (struct s8){
        .s = NULL,
        .l = 0,
    };
  }

  uint8_t *mem = calloc(len + 1, 1);
  memcpy(mem, s.s + start, len);
  return (struct s8){
      .s = mem,
      .l = len,
  };
}

void s8delete(struct s8 s) {
  if (s.s != NULL) {
    free(s.s);
  }
  s.l = 0;
  s.s = NULL;
}

struct s8 s8from_fmt(const char *fmt, ...) {
  va_list args;

  va_start(args, fmt);
  ssize_t len = vsnprintf(NULL, 0, fmt, args);
  va_end(args);

  if (len == -1) {
    return (struct s8){
        .s = NULL,
        .l = 0,
    };
  }

  char *buf = calloc(len + 1, 1);

  va_list args2;
  va_start(args2, fmt);
  vsnprintf(buf, len + 1, fmt, args2);
  va_end(args2);

  return (struct s8){
      .s = (uint8_t *)buf,
      .l = len,
  };
}

struct s8 s8join(struct s8 *s, size_t ns, uint8_t delimiter) {
  if (ns == 0) {
    return (struct s8){
        .s = NULL,
        .l = 0,
    };
  }

  size_t len = ns - 1;
  for (size_t i = 0; i < ns; ++i) {
    len += s[i].l;
  }

  uint8_t *mem = calloc(len + 1, 1);
  size_t offset = 0;
  for (size_t i = 0; i < ns; ++i) {
    memcpy(mem + offset, s[i].s, s[i].l);
    offset += s[i].l;

    if (offset < len) {
      mem[offset] = delimiter;
      ++offset;
    }
  }

  return (struct s8){
      .s = mem,
      .l = len,
  };
}

bool s8eq(struct s8 s1, struct s8 s2) {
  return s1.l == s2.l && memcmp(s1.s, s2.s, s1.l) == 0;
}

int s8cmp(struct s8 s1, struct s8 s2) {
  if (s1.l < s2.l) {
    int res = memcmp(s1.s, s2.s, s1.l);
    return res == 0 ? -s2.s[s1.l] : res;
  } else if (s2.l < s1.l) {
    int res = memcmp(s1.s, s2.s, s2.l);
    return res == 0 ? s1.s[s2.l] : res;
  }

  return memcmp(s1.s, s2.s, s1.l);
}

char *s8tocstr(struct s8 s) {
  char *cstr = (char *)malloc(s.l + 1);
  memcpy(cstr, s.s, s.l);
  cstr[s.l] = '\0';
  return cstr;
}

const char *s8ascstr(struct s8 s) { return (const char *)s.s; }

bool s8startswith(struct s8 s, struct s8 prefix) {
  if (prefix.l == 0 || prefix.l > s.l) {
    return false;
  }

  return memcmp(s.s, prefix.s, prefix.l) == 0;
}

bool s8endswith(struct s8 s, struct s8 suffix) {
  if (suffix.l > s.l) {
    return false;
  }

  size_t ldiff = s.l - suffix.l;
  return memcmp(s.s + ldiff, suffix.s, suffix.l) == 0;
}

struct s8 s8dup(struct s8 s) {
  struct s8 new = {0};

  new.l = s.l;
  new.s = (uint8_t *)calloc(s.l + 1, 1);
  memcpy(new.s, s.s, s.l);

  return new;
}

bool s8empty(struct s8 s) { return s.s == NULL || s.l == 0; }

bool s8onlyws(struct s8 s) {
  for (size_t i = 0; i < s.l; ++i) {
    if (!isspace(s.s[i])) {
      return false;
    }
  }

  return true;
}

uint8_t s8at(struct s8 s, size_t index) {
  if (index < s.l) {
    return s.s[index];
  }

  return 0;
}
ssize_t s8find(struct s8 s, uint8_t c) {
  for (size_t i = 0; i < s.l; ++i) {
    if (s.s[i] == c) {
      return i;
    }
  }

  return -1;
}

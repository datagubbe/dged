#include "s8.h"

#include "utf8.h"

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

static int memicmp(uint8_t *s1, uint8_t *s2, size_t n) {
  int res = 0;
  for (size_t i = 0; i < n; ++i) {
    uint8_t b1 = s1[i];
    uint8_t b2 = s2[i];

    res = b1 - b2;
    if (utf8_byte_is_ascii(b1) && utf8_byte_is_ascii(b2)) {
      if (b1 >= 'A' && b1 <= 'Z') {
        b1 += 0x20;
      }

      if (b2 >= 'A' && b2 <= 'Z') {
        b2 += 0x20;
      }

      res = b1 - b2;
    }

    if (res != 0) {
      return res;
    }
  }

  return res;
}

int s8icmp(struct s8 s1, struct s8 s2) {
  if (s1.l < s2.l) {
    int res = memicmp(s1.s, s2.s, s1.l);
    return res == 0 ? -s2.s[s1.l] : res;
  } else if (s2.l < s1.l) {
    int res = memicmp(s1.s, s2.s, s2.l);
    return res == 0 ? s1.s[s2.l] : res;
  }

  return memicmp(s1.s, s2.s, s1.l);
}

char *s8tocstr(struct s8 s) {
  char *cstr = (char *)malloc(s.l + 1);
  memcpy(cstr, s.s, s.l);
  cstr[s.l] = '\0';
  return cstr;
}

const char *s8ascstr(struct s8 s) { return (const char *)s.s; }

ssize_t s8findstr(struct s8 s, struct s8 find) {
  if (s8empty(s) || s8empty(find)) {
    return -1;
  }

  if (s.l < find.l) {
    return -1;
  }

  if (s.l == find.l) {
    return s8eq(s, find) ? 0 : -1;
  }

  /* at this point, s is longer than find */
  for (size_t i = 0; i < s.l; ++i) {
    uint8_t *start = &s.s[i];
    if (s8startswith((struct s8){.s = start, .l = s.l - i}, find)) {
      return i;
    }
  }

  return -1;
}

ssize_t s8ifindstr(struct s8 s, struct s8 find) {
  if (s8empty(s) || s8empty(find)) {
    return -1;
  }

  if (s.l < find.l) {
    return -1;
  }

  if (s.l == find.l) {
    return s8icmp(s, find) == 0 ? 0 : -1;
  }

  /* at this point, s is longer than find */
  for (size_t i = 0; i < s.l; ++i) {
    uint8_t *start = &s.s[i];
    if (s8istartswith((struct s8){.s = start, .l = s.l - i}, find)) {
      return i;
    }
  }

  return -1;
}

bool s8startswith(struct s8 s, struct s8 prefix) {
  if (prefix.l == 0 || prefix.l > s.l) {
    return false;
  }

  return memcmp(s.s, prefix.s, prefix.l) == 0;
}

bool s8istartswith(struct s8 s, struct s8 prefix) {
  if (prefix.l == 0 || prefix.l > s.l) {
    return false;
  }

  return memicmp(s.s, prefix.s, prefix.l) == 0;
}

bool s8endswith(struct s8 s, struct s8 suffix) {
  if (suffix.l > s.l) {
    return false;
  }

  size_t ldiff = s.l - suffix.l;
  return memcmp(s.s + ldiff, suffix.s, suffix.l) == 0;
}

bool s8iendswith(struct s8 s, struct s8 suffix) {
  if (suffix.l > s.l) {
    return false;
  }

  size_t ldiff = s.l - suffix.l;
  struct s8 tail = {
      .s = s.s + ldiff,
      .l = s.l - ldiff,
  };
  return s8icmp(tail, suffix) == 0;
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

static const char base64_enc_tbl[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

struct s8 s8base64enc(struct s8 s) {
  struct s8 output = {.l = 0, .s = 0};
  size_t output_length = 4 * ((s.l + 2) / 3);
  output.s = calloc(1, output_length + 1);
  if (output.s == NULL) {
    return output;
  }

  output.l = output_length;

  for (size_t i = 0, j = 0; i < s.l;) {
    uint32_t octet_a = i < s.l ? s.s[i] : 0;
    ++i;
    uint32_t octet_b = i < s.l ? s.s[i] : 0;
    ++i;
    uint32_t octet_c = i < s.l ? s.s[i] : 0;
    ++i;

    uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

    output.s[j++] = base64_enc_tbl[(triple >> 18) & 0x3F];
    output.s[j++] = base64_enc_tbl[(triple >> 12) & 0x3F];
    output.s[j++] = (i > s.l + 1) ? '=' : base64_enc_tbl[(triple >> 6) & 0x3F];
    output.s[j++] = (i > s.l) ? '=' : base64_enc_tbl[triple & 0x3F];
  }

  return output;
}

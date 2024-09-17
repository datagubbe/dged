#ifndef _S8_H
#define _S8_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

struct s8 {
  uint8_t *s;
  uint32_t l;
};

#define s8(s) ((struct s8){(uint8_t *)s, strlen(s)})

struct s8 s8new(const char *s, uint32_t len);
void s8delete(struct s8 s);
struct s8 s8from_fmt(const char *fmt, ...);
char *s8tocstr(struct s8 s);

bool s8eq(struct s8 s1, struct s8 s2);
int s8cmp(struct s8 s1, struct s8 s2);
bool s8startswith(struct s8 s, struct s8 prefix);
bool s8endswith(struct s8 s, struct s8 suffix);
struct s8 s8dup(struct s8 s);
bool s8empty(struct s8 s);
bool s8onlyws(struct s8 s);

#endif

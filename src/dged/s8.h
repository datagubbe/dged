#ifndef _S8_H
#define _S8_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

struct s8 {
  uint8_t *s;
  uint32_t l;
};

#define s8(s) ((struct s8){(uint8_t *)s, strlen(s)})

struct s8 s8new(const char *s, size_t len);
struct s8 s8substr(struct s8 s, size_t start, size_t end);
void s8delete(struct s8 s);
struct s8 s8from_fmt(const char *fmt, ...);
struct s8 s8join(struct s8 *s, size_t ns, uint8_t delimiter);

char *s8tocstr(struct s8 s);
const char *s8ascstr(struct s8 s);

uint8_t s8at(struct s8 s, size_t index);
ssize_t s8find(struct s8 s, uint8_t c);
ssize_t s8findstr(struct s8 s, struct s8 find);
bool s8eq(struct s8 s1, struct s8 s2);
int s8cmp(struct s8 s1, struct s8 s2);
int s8icmp(struct s8 s1, struct s8 s2);
bool s8startswith(struct s8 s, struct s8 prefix);
bool s8endswith(struct s8 s, struct s8 suffix);
struct s8 s8dup(struct s8 s);
bool s8empty(struct s8 s);
bool s8onlyws(struct s8 s);

struct s8 s8base64enc(struct s8 s);

#endif

#ifndef _S8_H
#define _S8_H

#include <stdbool.h>
#include <stdint.h>

#define s8(s) ((struct s8){(uint8_t *)s, strlen(s)})

struct s8 {
  uint8_t *s;
  uint32_t l;
};

bool s8eq(struct s8 s1, struct s8 s2);
int s8cmp(struct s8 s1, struct s8 s2);
char *s8tocstr(struct s8 s);
bool s8startswith(struct s8 s, struct s8 prefix);
struct s8 s8dup(struct s8 s);

#endif

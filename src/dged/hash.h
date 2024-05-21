#ifndef _HASH_H
#define _HASH_H

#include <stdint.h>

#include "s8.h"

uint32_t hash_name(const char *s);
uint32_t hash_name_s8(struct s8 s);

#endif

#ifndef _COMPLETION_MATCHERS_H
#define _COMPLETION_MATCHERS_H

#include <stdbool.h>

#include "dged/s8.h"

typedef bool (*filter_fn)(struct s8 needle, struct s8 item, size_t *match_begin,
                          size_t *match_end, uint32_t *score);

bool filter_exact(struct s8 needle, struct s8 item, size_t *match_begin,
                  size_t *match_end, uint32_t *score);

bool filter_contains(struct s8 needle, struct s8 item, size_t *match_begin,
                     size_t *match_end, uint32_t *score);

bool filter_fuzzy(struct s8 needle, struct s8 item, size_t *match_begin,
                  size_t *match_end, uint32_t *score);

#endif

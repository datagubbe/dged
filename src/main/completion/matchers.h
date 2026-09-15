#ifndef _COMPLETION_MATCHERS_H
#define _COMPLETION_MATCHERS_H

#include <stdbool.h>

#include "dged/s8.h"

struct match {
  size_t begin;
  size_t end;
  uint32_t score;
};

typedef size_t (*filter_fn)(struct s8 needle, struct s8 item,
                            struct match *matches, size_t max_nmatches);

size_t filter_exact(struct s8 needle, struct s8 item, struct match *matches,
                    size_t max_nmatches);

size_t filter_contains(struct s8 needle, struct s8 item, struct match *matches,
                       size_t max_nmatches);

size_t filter_fuzzy(struct s8 needle, struct s8 item, struct match *matches,
                    size_t max_nmatches);

#endif

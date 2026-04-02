#include "matchers.h"

#include "dged/s8.h"

bool filter_exact(struct s8 needle, struct s8 item, size_t *match_begin,
                  size_t *match_end, uint32_t *score) {

  if (needle.l == 0) {
    *match_begin = 0;
    *match_end = 0;
    *score = 0;
    return true;
  }

  if (s8istartswith(item, needle)) {
    *match_begin = 0;
    *match_end = needle.l;
    *score = 1;

    return true;
  }

  return false;
}

bool filter_contains(struct s8 needle, struct s8 item, size_t *match_begin,
                     size_t *match_end, uint32_t *score) {

  if (needle.l == 0) {
    *match_begin = 0;
    *match_end = 0;
    *score = 0;
    return true;
  }

  ssize_t res = -1;
  if ((res = s8ifindstr(item, needle)) != -1) {

    *match_begin = res;
    *match_end = res + needle.l;
    *score = 1;

    return true;
  }

  return false;
}

bool filter_fuzzy(struct s8 needle, struct s8 item, size_t *match_begin,
                  size_t *match_end, uint32_t *score) {

  // TODO: Implement some distance measure.
  return filter_contains(needle, item, match_begin, match_end, score);
}

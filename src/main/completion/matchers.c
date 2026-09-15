#include "matchers.h"

#include <ctype.h>

#include "dged/minibuffer.h"
#include "dged/s8.h"

size_t filter_exact(struct s8 needle, struct s8 item, struct match *matches,
                    size_t max_nmatches) {
  if (s8istartswith(item, needle) && max_nmatches > 0) {
    matches[0].begin = 0;
    matches[0].end = needle.l;
    matches[0].score = 1;
    return 1;
  }

  return 0;
}

size_t filter_contains(struct s8 needle, struct s8 item, struct match *matches,
                       size_t max_nmatches) {
  ssize_t res = -1;
  size_t nmatches = 0, idx = 0;
  while ((res = s8ifindstrat(item, needle, idx)) != -1) {

    if (nmatches == max_nmatches) {
      return nmatches;
    }

    matches[nmatches].begin = res;
    matches[nmatches].end = res + needle.l;
    matches[nmatches].score = 1;

    ++nmatches;
    idx = res + needle.l;
  }

  return nmatches;
}

size_t filter_fuzzy(struct s8 needle, struct s8 item, struct match *matches,
                    size_t max_nmatches) {

  size_t ni = 0, i = 0;
  size_t nmatches = 0;

  while (ni < needle.l && i < item.l) {
    unsigned char wanted = tolower(needle.s[ni]);
    unsigned char current = tolower(item.s[i]);

    if (wanted == current) {
      if (nmatches > 0 && matches[nmatches - 1].end == i) {
        ++matches[nmatches - 1].score;
        matches[nmatches - 1].end = i + 1;
      } else {
        if (nmatches == max_nmatches) {
          return nmatches;
        }

        matches[nmatches].begin = i;
        matches[nmatches].score = 1;
        matches[nmatches].end = i + 1;

        ++nmatches;
      }

      ++ni;
    }

    ++i;
  }

  if (ni != needle.l) {
    return 0;
  }

  return nmatches;
}

#include "assert.h"
#include "test.h"

#include "main/completion/matchers.h"

static void test_exact_matcher(void) {
  struct match matches[16];

  size_t nmatches = filter_exact(s8("sune"), s8("sunebune"), matches, 16);

  ASSERT(nmatches == 1, "Expected exactly one match");
  ASSERT(matches[0].begin == 0 && matches[0].end == 4,
         "Expected to match the initial four chars");

  nmatches = filter_exact(s8("bune"), s8("sunebune"), matches, 16);
  ASSERT(nmatches == 0, "Expected no match");
}

static void test_contains_matcher(void) {
  struct match matches[16];

  size_t nmatches = filter_contains(s8("un"), s8("sunebune"), matches, 16);

  ASSERT(nmatches == 2, "Expected two matches");
  ASSERT(matches[0].begin == 1 && matches[0].end == 3,
         "Expected to match the middle chars in sune");
  ASSERT(matches[1].begin == 5 && matches[1].end == 7,
         "Expected to match the middle chars in bune");

  nmatches = filter_contains(s8("x"), s8("sune"), matches, 16);
  ASSERT(nmatches == 0, "Expected no match");
}

static void test_fuzzy_matcher(void) {
  struct match matches[16];

  size_t nmatches = filter_fuzzy(s8("un"), s8("suen/bune"), matches, 16);

  /* the matcher is quite simplistic so will only match
   * the first occurence of each of the characters in
   * the needle
   */
  ASSERT(nmatches == 2, "Expected two matches");
  ASSERT(matches[0].begin == 1 && matches[0].end == 2,
         "Expected to match the first u in sune");
  ASSERT(matches[1].begin == 3 && matches[1].end == 4,
         "Expected to match the first n in sune");
}

void run_matcher_tests(void) {
  run_test(test_exact_matcher);
  run_test(test_contains_matcher);
  run_test(test_fuzzy_matcher);
}

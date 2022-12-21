#include "utf8.h"
#include "assert.h"
#include "test.h"
#include "wchar.h"

#include <string.h>

void test_nchars_nbytes() {
  ASSERT(utf8_nchars((uint8_t *)"👴", strlen("👴")) == 1,
         "Expected old man emoji to be 1 char");
  ASSERT(utf8_nbytes((uint8_t *)"👴", strlen("👴"), 1) == 4,
         "Expected old man emoji to be 4 bytes");
}

void run_utf8_tests() { run_test(test_nchars_nbytes); }

#include <string.h>

#include "dged/buffer.h"

#include "assert.h"
#include "test.h"

void test_add() {
  struct buffer b = buffer_create("test-buffer");
  ASSERT(buffer_num_lines(&b) == 0, "Expected buffer to have zero lines");

  const char *txt = "we are adding some text";
  struct location loc = buffer_add(&b, (struct location){.line = 1, .col = 0},
                                   (uint8_t *)txt, strlen(txt));

  ASSERT(loc.line == 1 && loc.col == strlen(txt),
         "Expected buffer to have one line with characters");
}

void run_buffer_tests() { run_test(test_add); }

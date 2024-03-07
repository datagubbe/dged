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

  buffer_destroy(&b);
}

void test_word_at() {
  struct buffer b = buffer_create("test-word-at-buffer");
  const char *txt = "word1 (word2). Another";
  buffer_add(&b, (struct location){.line = 0, .col = 0}, (uint8_t *)txt,
             strlen(txt));

  struct region word1 =
      buffer_word_at(&b, (struct location){.line = 0, .col = 0});
  ASSERT(region_has_size(word1), "expected 0,0 to be a word");
  ASSERT(word1.begin.col == 0 && word1.end.col == 5,
         "Expected word to end at col 5");

  // test that dot can be in the middle of a word
  // and that '(' and ')' works as a delimiter
  struct region word2 =
      buffer_word_at(&b, (struct location){.line = 0, .col = 8});
  ASSERT(region_has_size(word2), "expected 0,8 to be in a word");
  ASSERT(word2.begin.col == 7 && word2.end.col == 12,
         "Expected word to span cols 7..12");

  // test that clamping works correctly
  struct region word3 =
      buffer_word_at(&b, (struct location){.line = 0, .col = 100});
  ASSERT(region_has_size(word3), "expected 0,100 to be in the last word");
  ASSERT(word3.begin.col == 15 && word3.end.col == 22,
         "Expected word to span cols 15..22");

  buffer_destroy(&b);
}

void run_buffer_tests() {
  run_test(test_add);
  run_test(test_word_at);
}

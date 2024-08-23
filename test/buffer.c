#include <string.h>

#include "dged/buffer.h"
#include "dged/settings.h"

#include "assert.h"
#include "test.h"

static uint32_t add_callback_call_count = 0;
static void add_callback(struct buffer *buffer, struct edit_location added,
                         void *userdata) {
  (void)buffer;
  (void)added;
  (void)userdata;
  ++add_callback_call_count;
}

static void test_add(void) {
  struct buffer b = buffer_create("test-buffer");
  ASSERT(buffer_num_lines(&b) == 0, "Expected buffer to have zero lines");

  const char *txt = "we are adding some text";
  struct location loc = buffer_add(&b, (struct location){.line = 1, .col = 0},
                                   (uint8_t *)txt, strlen(txt));

  ASSERT(loc.line == 1 && loc.col == strlen(txt),
         "Expected buffer to have one line with characters");

  // test callback
  uint32_t hook_id = buffer_add_insert_hook(&b, add_callback, NULL);
  buffer_add(&b, (struct location){.line = 0, .col = 0}, (uint8_t *)"hej", 3);
  ASSERT(add_callback_call_count == 1, "Expected callback to have been called");

  // test removing the hook
  buffer_remove_insert_hook(&b, hook_id, NULL);
  buffer_add(&b, (struct location){.line = 0, .col = 0}, (uint8_t *)"hej", 3);
  ASSERT(add_callback_call_count == 1,
         "Expected callback to not have been called after it has been removed");

  buffer_destroy(&b);
}

static uint32_t delete_callback_call_count = 0;
static void delete_callback(struct buffer *buffer, struct edit_location removed,
                            void *userdata) {
  (void)buffer;
  (void)removed;
  (void)userdata;
  ++delete_callback_call_count;
}

static void test_delete(void) {
  struct buffer b = buffer_create("test-buffer-delete");
  const char *txt = "we are adding some text\ntwo lines to be exact";
  struct location loc = buffer_add(&b, (struct location){.line = 0, .col = 0},
                                   (uint8_t *)txt, strlen(txt));

  ASSERT(buffer_line_length(&b, 0) == 23,
         "Expected line 1 to be 23 chars before deletion");
  buffer_delete(&b, region_new((struct location){.line = 0, .col = 0},
                               (struct location){.line = 0, .col = 2}));
  ASSERT(buffer_line_length(&b, 0) == 21,
         "Expected line 1 to be 21 chars after deletion");

  // delete newline
  buffer_delete(&b, region_new((struct location){.line = 0, .col = 21},
                               (struct location){.line = 1, .col = 0}));
  ASSERT(buffer_num_lines(&b) == 1,
         "Expected buffer to have one line after new line deletion");
  ASSERT(buffer_line_length(&b, 0) == 42,
         "Expected single line to be sum of both line lengths after new line "
         "deletion");

  // test that callback works
  buffer_add_delete_hook(&b, delete_callback, NULL);
  buffer_delete(&b, region_new((struct location){.line = 0, .col = 0},
                               (struct location){.line = 0, .col = 2}));
  ASSERT(delete_callback_call_count == 1,
         "Expected callback to have been called");

  buffer_destroy(&b);
}

static void test_word_at(void) {
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
  struct region word3 = buffer_word_at(&b, buffer_clamp(&b, 0, 100));
  ASSERT(region_has_size(word3), "expected 0,100 to be in the last word");
  ASSERT(word3.begin.col == 15 && word3.end.col == 22,
         "Expected word to span cols 15..22");

  buffer_destroy(&b);
}

static void test_line_len(void) {
  struct buffer b = buffer_create("test-line-length-buffer");
  const char *txt = "Look! Banana 🍌";
  buffer_add(&b, (struct location){.line = 0, .col = 0}, (uint8_t *)txt,
             strlen(txt));
  ASSERT(buffer_line_length(&b, 0) == 15,
         "Expected banana line to be 15 chars wide");
}

static void test_char_movement(void) {
  struct buffer b = buffer_create("test-char-movement-buffer");
  const char *txt = "abcdefgh 🎯jklmn\tab";
  buffer_add(&b, buffer_end(&b), (uint8_t *)txt, strlen(txt));
  struct location next =
      buffer_next_char(&b, (struct location){.line = 0, .col = 0});
  ASSERT(next.col == 1, "Expected next char to be next char");

  next = buffer_next_char(&b, (struct location){.line = 0, .col = 9});
  ASSERT(next.col == 11,
         "Expected a double width char to result in a 2 column move");

  next = buffer_next_char(&b, (struct location){.line = 0, .col = 16});
  uint64_t tab_width = settings_get("editor.tab-width")->value.number_value;
  ASSERT(next.col == 16 + tab_width,
         "Expected a tab to result in a move the width of a tab");

  struct location prev =
      buffer_previous_char(&b, (struct location){.line = 0, .col = 0});
  ASSERT(prev.col == 0 && prev.line == 0,
         "Expected backwards motion from 0,0 not to be possible");

  prev = buffer_previous_char(&b, (struct location){.line = 0, .col = 11});
  ASSERT(prev.col == 9,
         "Expected a double width char to result in a 2 column move");

  prev = buffer_previous_char(
      &b, (struct location){.line = 0, .col = 16 + tab_width});
  ASSERT(prev.col == 16,
         "Expected a tab move backwards to step over the width of a tab");
}

static void test_word_movement(void) {
  struct buffer b = buffer_create("test-word-movement-buffer");

  const char *txt = " word1, word2 \"word3\" word4";
  buffer_add(&b, buffer_end(&b), (uint8_t *)txt, strlen(txt));
  struct location next =
      buffer_next_word(&b, (struct location){.line = 0, .col = 0});
  ASSERT(next.col == 1, "Expected next word to start at col 1");

  next = buffer_next_word(&b, (struct location){.line = 0, .col = 1});
  ASSERT(next.col == 8, "Expected next word to start at col 8");

  next = buffer_next_word(&b, (struct location){.line = 0, .col = 8});
  ASSERT(next.col == 15, "Expected next word to start at col 15");

  next = buffer_next_word(&b, (struct location){.line = 0, .col = 15});
  ASSERT(next.col == 22, "Expected next word to start at col 22");

  struct location prev =
      buffer_previous_word(&b, (struct location){.line = 0, .col = 26});
  ASSERT(prev.col == 22, "Expected previous word to start at col 22");

  prev = buffer_previous_word(&b, (struct location){.line = 0, .col = 22});
  ASSERT(prev.col == 15, "Expected previous word to start at col 15");

  prev = buffer_previous_word(&b, (struct location){.line = 0, .col = 0});
  ASSERT(prev.col == 0 && prev.line == 0,
         "Expected previous word to not go before beginning of buffer");
}

void test_copy(void) {
  struct buffer b = buffer_create("test-copy-buffer");
  buffer_add(&b, (struct location){.line = 0, .col = 0}, (uint8_t *)"copy", 4);

  buffer_copy(&b, region_new((struct location){.line = 0, .col = 0},
                             (struct location){.line = 0, .col = 4}));
  buffer_paste(&b, (struct location){.line = 0, .col = 4});
  ASSERT(buffer_line_length(&b, 0) == 8, "Expected text to be copied");
  struct text_chunk t = buffer_line(&b, 0);
  ASSERT_STR_EQ((const char *)t.text, "copycopy",
                "Expected copied text to match");
  if (t.allocated) {
    free(t.text);
  }

  buffer_cut(&b, region_new((struct location){.line = 0, .col = 2},
                            (struct location){.line = 0, .col = 4}));
  buffer_paste(&b, (struct location){.line = 0, .col = 0});
  ASSERT(buffer_line_length(&b, 0) == 8, "Expected line length to be the same");
  t = buffer_line(&b, 0);
  ASSERT_STR_EQ((const char *)t.text, "pycocopy",
                "Expected cut+pasted text to match");
  if (t.allocated) {
    free(t.text);
  }

  // test kill ring
  buffer_paste_older(&b, (struct location){.line = 0, .col = 0});
  ASSERT(buffer_line_length(&b, 0) == 12,
         "Expected line length to have increased when pasting older");
  t = buffer_line(&b, 0);
  ASSERT_STR_EQ((const char *)t.text, "copypycocopy",
                "Expected pasted older text to match");
  if (t.allocated) {
    free(t.text);
  }

  buffer_destroy(&b);
}

void run_buffer_tests(void) {
  settings_init(10);
  settings_set_default(
      "editor.tab-width",
      (struct setting_value){.type = Setting_Number, .number_value = 4});

  run_test(test_add);
  run_test(test_delete);
  run_test(test_word_at);
  run_test(test_line_len);
  run_test(test_char_movement);
  run_test(test_word_movement);
  run_test(test_copy);
  settings_destroy();
}

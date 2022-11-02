#include "assert.h"
#include "test.h"

#include "text.h"

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

void assert_line_equal(struct txt_line *line) {}

void test_add_text() {
  uint32_t lines_added, cols_added;
  struct text *t = text_create(10);
  const char *txt = "This is line 1\n";
  text_append(t, 0, 0, (uint8_t *)txt, strlen(txt), &lines_added, &cols_added);
  ASSERT(text_num_lines(t) == 2,
         "Expected text to have two lines after insertion");

  ASSERT(text_line_size(t, 0) == 14 && text_line_length(t, 0) == 14,
         "Expected line 1 to have 14 chars and 14 bytes");
  ASSERT_STR_EQ((const char *)text_get_line(t, 0).text, "This is line 1",
                "Expected line 1 to be line 1");

  const char *txt2 = "This is line 2\n";
  text_append(t, 1, 0, (uint8_t *)txt2, strlen(txt2), &lines_added,
              &cols_added);
  ASSERT_STR_EQ((const char *)text_get_line(t, 1).text, "This is line 2",
                "Expected line 2 to be line 2");
}

void test_delete_text() {
  uint32_t lines_added, cols_added;
  struct text *t = text_create(10);
  const char *txt = "This is line 1";
  text_append(t, 0, 0, (uint8_t *)txt, strlen(txt), &lines_added, &cols_added);

  text_delete(t, 0, 12, 2);
  ASSERT(text_line_length(t, 0) == 12,
         "Expected line to be 12 chars after deleting two");
  ASSERT(strncmp((const char *)text_get_line(t, 0).text, "This is line",
                 text_line_size(t, 0)) == 0,
         "Expected two chars to be deleted");

  text_delete(t, 0, 0, 25);
  ASSERT(text_get_line(t, 0).nbytes == 0,
         "Expected line to be empty after many chars removed");

  const char *txt2 = "This is line 1\nThis is line 2\nThis is line 3";
  text_append(t, 0, 0, (uint8_t *)txt2, strlen(txt2), &lines_added,
              &cols_added);
  text_delete(t, 1, 11, 3);
  ASSERT(text_line_length(t, 1) == 11,
         "Expected line to contain 11 chars after deletion");
  struct txt_line line = text_get_line(t, 1);
  ASSERT(strncmp((const char *)line.text, "This is lin", line.nbytes) == 0,
         "Expected deleted characters to be gone in the second line");

  // test utf-8
  struct text *t2 = text_create(10);
  const char *txt3 = "Emojis: 🇫🇮 🐮\n";
  text_append(t2, 0, 0, (uint8_t *)txt3, strlen(txt3), &lines_added,
              &cols_added);

  // TODO: Fix when graphemes are implemented, should be 11, right now it counts
  // the two unicode code points 🇫 and 🇮 as two chars.
  ASSERT(text_line_length(t2, 0) == 12,
         "Line length should be 12 (even though there "
         "are more bytes in the line).");

  text_delete(t2, 0, 10, 2);
  ASSERT(text_line_length(t2, 0) == 10,
         "Line length should be 10 after deleting the cow emoji and a space");
  struct txt_line line2 = text_get_line(t2, 0);
  ASSERT(strncmp((const char *)line2.text, "Emojis: 🇫🇮", line2.nbytes) == 0,
         "Expected cow emoji plus space to be deleted");
}

void run_text_tests() {
  run_test(test_add_text);
  run_test(test_delete_text);
}

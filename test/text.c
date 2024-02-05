#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "dged/text.h"

#include "assert.h"
#include "stdio.h"
#include "test.h"

void assert_line_eq(struct text_chunk line, const char *txt, const char *msg) {
  ASSERT(strncmp((const char *)line.text, txt, line.nbytes) == 0, msg);
}

void assert_line_equal(struct text_chunk *line) {}

void test_add_text() {
  uint32_t lines_added, cols_added;
  /* use a silly small initial capacity to test re-alloc */
  struct text *t = text_create(1);

  const char *txt = "This is line 1\n";
  text_insert_at(t, 0, 0, (uint8_t *)txt, strlen(txt), &lines_added,
                 &cols_added);

  ASSERT(text_line_size(t, 0) == 14 && text_line_length(t, 0) == 14,
         "Expected line 1 to have 14 chars and 14 bytes");
  assert_line_eq(text_get_line(t, 0), "This is line 1",
                 "Expected line 1 to be line 1");

  const char *txt2 = "This is line 2\n";
  text_insert_at(t, 1, 0, (uint8_t *)txt2, strlen(txt2), &lines_added,
                 &cols_added);
  ASSERT(text_num_lines(t) == 3,
         "Expected text to have three lines after second insertion");
  assert_line_eq(text_get_line(t, 1), "This is line 2",
                 "Expected line 2 to be line 2");

  // simulate indentation
  const char *txt3 = "    ";
  text_insert_at(t, 0, 0, (uint8_t *)txt3, strlen(txt3), &lines_added,
                 &cols_added);
  ASSERT(text_num_lines(t) == 3,
         "Expected text to have three lines after second insertion");
  assert_line_eq(text_get_line(t, 0), "    This is line 1",
                 "Expected line 1 to be indented");
  assert_line_eq(text_get_line(t, 1), "This is line 2",
                 "Expected line 2 to be line 2 still");

  // insert newline in middle of line
  text_insert_at(t, 1, 4, (uint8_t *)"\n", 1, &lines_added, &cols_added);
  ASSERT(text_num_lines(t) == 4,
         "Expected text to have four lines after inserting a new line");
  assert_line_eq(text_get_line(t, 1), "This", "Expected line 2 to be split");
  assert_line_eq(text_get_line(t, 2), " is line 2",
                 "Expected line 2 to be split");

  // insert newline before line 1
  text_insert_at(t, 1, 0, (uint8_t *)"\n", 1, &lines_added, &cols_added);
  ASSERT(
      text_num_lines(t) == 5,
      "Expected to have five lines after adding an empty line in the middle");
  ASSERT(text_line_length(t, 1) == 0, "Expected line 2 to be empty");
  assert_line_eq(text_get_line(t, 2), "This",
                 "Expected line 3 to be previous line 2");
  assert_line_eq(text_get_line(t, 3), " is line 2",
                 "Expected line 4 to be previous line 3");

  text_destroy(t);
}

void test_delete_text() {
  uint32_t lines_added, cols_added;
  struct text *t = text_create(10);
  const char *txt = "This is line 1";
  text_insert_at(t, 0, 0, (uint8_t *)txt, strlen(txt), &lines_added,
                 &cols_added);

  text_delete(t, 0, 12, 0, 14);
  ASSERT(text_line_length(t, 0) == 12,
         "Expected line to be 12 chars after deleting two");
  ASSERT(strncmp((const char *)text_get_line(t, 0).text, "This is line",
                 text_line_size(t, 0)) == 0,
         "Expected two chars to be deleted");

  text_delete(t, 0, 0, 10, 10);
  ASSERT(text_get_line(t, 0).nbytes == 0,
         "Expected line to be empty after many chars removed");

  const char *txt2 = "This is line 1\nThis is line 2\nThis is line 3";
  text_insert_at(t, 0, 0, (uint8_t *)txt2, strlen(txt2), &lines_added,
                 &cols_added);
  ASSERT(text_num_lines(t) == 3,
         "Expected to have three lines after inserting as many");

  text_delete(t, 1, 11, 1, 14);
  ASSERT(text_line_length(t, 1) == 11,
         "Expected line to contain 11 chars after deletion");
  struct text_chunk line = text_get_line(t, 1);
  ASSERT(strncmp((const char *)line.text, "This is lin", line.nbytes) == 0,
         "Expected deleted characters to be gone in the second line");

  text_delete(t, 1, 0, 1, text_line_length(t, 1) + 1);
  ASSERT(text_num_lines(t) == 2,
         "Expected to have two lines after deleting one");
  struct text_chunk line2 = text_get_line(t, 1);
  ASSERT(strncmp((const char *)line2.text, "This is line 3", line2.nbytes) == 0,
         "Expected lines to have shifted upwards after deleting");

  struct text *t3 = text_create(10);
  const char *delete_me = "This is line🎙\nQ";
  text_insert_at(t3, 0, 0, (uint8_t *)delete_me, strlen(delete_me),
                 &lines_added, &cols_added);
  text_delete(t3, 0, 13, 0, 14);
  struct text_chunk top_line = text_get_line(t3, 0);
  ASSERT(strncmp((const char *)top_line.text, "This is line🎙Q",
                 top_line.nbytes) == 0,
         "Expected text from second line to be appended to first line when "
         "deleting newline");
  ASSERT(text_num_lines(t3) == 1,
         "Expected text to have one line after deleting newline");

  struct text *t4 = text_create(10);
  const char *deletable_text = "Only one line kinda";
  text_append(t4, (uint8_t *)deletable_text, strlen(deletable_text),
              &lines_added, &cols_added);
  text_delete(t4, 0, 19, 0, 20);
  ASSERT(text_num_lines(t4) == 1, "Expected the line to still be there");
  ASSERT(text_line_length(t4, 0) == 19,
         "Expected nothing to have happened to the line");

  // test utf-8
  struct text *t2 = text_create(10);
  const char *txt3 = "Emojis: 🇫🇮 🐮\n";
  text_insert_at(t2, 0, 0, (uint8_t *)txt3, strlen(txt3), &lines_added,
                 &cols_added);

  // TODO: Fix when graphemes are implemented, should be 11, right now it counts
  // the two unicode code points 🇫 and 🇮 as two chars.
  ASSERT(text_line_length(t2, 0) == 12,
         "Line length should be 12 (even though there "
         "are more bytes in the line).");

  text_delete(t2, 0, 10, 0, 12);
  ASSERT(text_line_length(t2, 0) == 10,
         "Line length should be 10 after deleting the cow emoji and a space");
  struct text_chunk line3 = text_get_line(t2, 0);
  ASSERT(strncmp((const char *)line3.text, "Emojis: 🇫🇮", line3.nbytes) == 0,
         "Expected cow emoji plus space to be deleted");

  text_destroy(t);
  text_destroy(t2);
  text_destroy(t3);
  text_destroy(t4);
}

void run_text_tests() {
  run_test(test_add_text);
  run_test(test_delete_text);
}

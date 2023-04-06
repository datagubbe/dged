#include <string.h>

#include "dged/buffer.h"

#include "assert.h"
#include "test.h"

void test_move() {
  struct buffer b = buffer_create("test-buffer");
  struct buffer_view v = buffer_view_create(&b, false, false);
  ASSERT(v.dot.col == 0 && v.dot.line == 0,
         "Expected dot to be at buffer start");

  // make sure we cannot move now
  buffer_backward_char(&v);
  buffer_backward_line(&v);
  ASSERT(v.dot.col == 0 && v.dot.line == 0,
         "Expected to not be able to move backward in empty buffer");

  buffer_forward_char(&v);
  buffer_forward_line(&v);
  ASSERT(v.dot.col == 0 && v.dot.line == 0,
         "Expected to not be able to move forward in empty buffer");

  // add some text and try again
  const char *txt = "testing movement";
  int lineindex = buffer_add_text(&v, (uint8_t *)txt, strlen(txt));
  ASSERT(lineindex + 1 == 1, "Expected buffer to have one line");

  buffer_beginning_of_line(&v);
  buffer_forward_char(&v);
  ASSERT(v.dot.col == 1 && v.dot.line == 0,
         "Expected to be able to move forward by one char");

  // now we have two lines
  const char *txt2 = "\n";
  int lineindex2 = buffer_add_text(&v, (uint8_t *)txt2, strlen(txt2));
  ASSERT(lineindex2 + 1 == 2, "Expected buffer to have two lines");
  buffer_backward_line(&v);
  buffer_beginning_of_line(&v);
  buffer_backward_char(&v);
  ASSERT(
      v.dot.col == 0 && v.dot.line == 0,
      "Expected to not be able to move backwards when at beginning of buffer");

  buffer_destroy(&b);
}

void run_buffer_tests() { run_test(test_move); }

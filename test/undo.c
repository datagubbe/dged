#include <stdlib.h>

#include "dged/undo.h"

#include "assert.h"
#include "test.h"

void test_undo_insert(void) {
  struct undo_stack undo;

  /* small capacity on purpose to force re-sizing */
  undo_init(&undo, 1);

  undo_push_boundary(&undo, (struct undo_boundary){.save_point = true});
  ASSERT(undo_size(&undo) == 1,
         "Expected undo stack to have one item after inserting a save point");

  undo_push_boundary(&undo, (struct undo_boundary){0});
  ASSERT(undo_size(&undo) == 2,
         "Expected undo stack to have two items after inserting a boundary");

  undo_push_add(&undo, (struct undo_add){.begin = {.col = 0, .row = 0},
                                         .end = {.col = 4, .row = 0}});
  ASSERT(undo_size(&undo) == 3,
         "Expected undo stack to have three items after inserting an add");

  undo_push_delete(&undo, (struct undo_delete){.pos = {.row = 0, .col = 3},
                                               .data = NULL,
                                               .nbytes = 0});

  ASSERT(undo_size(&undo) == 4,
         "Expected undo stack to have four items after inserting a delete");

  ASSERT(undo_current_position(&undo) == undo_size(&undo) - 1,
         "Undo stack position should be at the top after inserting");

  undo_destroy(&undo);
}

void test_undo(void) {
  struct undo_stack undo;
  undo_init(&undo, 10);

  undo_push_boundary(&undo, (struct undo_boundary){.save_point = true});
  undo_push_add(&undo, (struct undo_add){.begin = {.row = 0, .col = 10},
                                         .end = {.row = 2, .col = 3}});

  struct undo_record *records = NULL;
  uint32_t nrecords = 0;

  undo_next(&undo, &records, &nrecords);
  ASSERT(nrecords == 2, "Expected to get back two records");
  ASSERT(records[0].type == Undo_Add,
         "Expected first returned record to be an add");
  free(records);

  ASSERT(undo_current_position(&undo) == INVALID_TOP,
         "Expected undo stack position to have changed after undo");

  // check that undo begin causes the top of the stack to not be reset
  undo_begin(&undo);
  undo_push_add(&undo, (struct undo_add){.begin = {.row = 0, .col = 10},
                                         .end = {.row = 0, .col = 12}});
  undo_end(&undo);

  ASSERT(undo_current_position(&undo) == INVALID_TOP,
         "Expected undo stack position to not have changed when undo "
         "information was added as part of an undo");

  // but now it should
  undo_push_add(&undo, (struct undo_add){.begin = {.row = 0, .col = 10},
                                         .end = {.row = 0, .col = 12}});

  ASSERT(undo_current_position(&undo) == 3,
         "Expected undo stack position to have changed when undo information "
         "was added");

  // test that it gets reset to top
  undo_begin(&undo);
  records = NULL;
  undo_next(&undo, &records, &nrecords);
  free(records);

  undo_push_add(&undo, (struct undo_add){.begin = {.row = 0, .col = 10},
                                         .end = {.row = 0, .col = 12}});
  undo_push_boundary(&undo, (struct undo_boundary){.save_point = false});
  undo_push_add(&undo, (struct undo_add){.begin = {.row = 0, .col = 10},
                                         .end = {.row = 0, .col = 12}});

  records = NULL;
  undo_next(&undo, &records, &nrecords);
  free(records);

  undo_end(&undo);
  ASSERT(
      undo_current_position(&undo) == 4,
      "Expected undo stack position to have been reset when it reached zero");

  undo_destroy(&undo);
}

void run_undo_tests(void) {
  run_test(test_undo_insert);
  run_test(test_undo);
}

#include "assert.h"
#include "test.h"

#include "allocator.h"

void test_frame_allocator() {
  struct frame_allocator fa = frame_allocator_create(128);

  ASSERT(fa.capacity == 128,
         "Expected frame allocator to be created with specified capacity");

  void *bytes = frame_allocator_alloc(&fa, 128);
  ASSERT(
      bytes != NULL,
      "Expected to be able to allocate <capacity> bytes from frame allocator");

  void *bytes_again = frame_allocator_alloc(&fa, 128);
  ASSERT(bytes_again == NULL,
         "Expected to not be able to allocate <capacity> bytes "
         "from frame allocator a second time");

  frame_allocator_clear(&fa);
  void *bytes_after_clear = frame_allocator_alloc(&fa, 128);
  ASSERT(bytes_after_clear != NULL,
         "Expected to be able to allocate <capacity> bytes from frame "
         "allocator again after clearing it");

  frame_allocator_destroy(&fa);
}

void run_allocator_tests() { run_test(test_frame_allocator); }

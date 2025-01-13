#include "assert.h"
#include "test.h"

#include "dged/buffer.h"
#include "dged/buffers.h"

struct hook_call_args {
  struct buffer *buffer[64];
  size_t call_count;
};

static void add_remove_hook(struct buffer *buffer, void *userdata) {
  struct hook_call_args *args = (struct hook_call_args *)userdata;

  if (args->call_count < 64) {
    args->buffer[args->call_count] = buffer;
  }

  ++args->call_count;
}

static void add(void) {
  struct buffers buflist;
  buffers_init(&buflist, 16);

  ASSERT(buffers_num_buffers(&buflist) == 0,
         "Expected buffer list to be empty upon creation.");

  struct buffer b1 = buffer_create("b1");
  struct buffer b2 = buffer_create("b2");

  struct hook_call_args hook_args = {0};
  buffers_add_add_hook(&buflist, add_remove_hook, &hook_args);

  struct buffer *bp1 = buffers_add(&buflist, b1);
  struct buffer *bp2 = buffers_add(&buflist, b2);

  ASSERT(hook_args.call_count == 2,
         "Expected add hook to be called two times when adding two buffers.");
  ASSERT(hook_args.buffer[0] == bp1,
         "Expected add hook to be called with first buffer first.");
  ASSERT(hook_args.buffer[1] == bp2,
         "Expected add hook to be called with second buffer second.");

  buffers_destroy(&buflist);
}

static void find(void) {
  struct buffers buflist;
  buffers_init(&buflist, 16);

  struct buffer b1 = buffer_create("b1");
  struct buffer b2 = buffer_create("b2");
  struct buffer b3 = buffer_create("b3");
  buffer_set_filename(&b3, "b3.txt");

  struct buffer *bp1 = buffers_add(&buflist, b1);
  struct buffer *bp2 = buffers_add(&buflist, b2);
  struct buffer *bp3 = buffers_add(&buflist, b3);

  struct buffer *found1 = buffers_find(&buflist, "b1");
  ASSERT(found1 == bp1, "Expected to find equally named buffer");

  struct buffer *found2 = buffers_find(&buflist, "b2");
  ASSERT(found2 == bp2, "Expected to find equally named buffer");

  struct buffer *ne = buffers_find(&buflist, "nonsense");
  ASSERT(ne == NULL, "Expected to not find non-existent buffer");

  struct buffer *found3 = buffers_find_by_filename(&buflist, "b3.txt");
  ASSERT(found3 == bp3,
         "Expected to be able to find buffer 3 by filename since it has one.");

  buffers_destroy(&buflist);
}

static void remove_buffer() {
  struct buffers buflist;
  buffers_init(&buflist, 16);

  struct buffer b1 = buffer_create("b1");
  struct buffer b2 = buffer_create("b2");

  buffers_add(&buflist, b1);
  struct buffer *bp2 = buffers_add(&buflist, b2);

  struct hook_call_args hook_args = {0};
  buffers_add_remove_hook(&buflist, add_remove_hook, &hook_args);

  bool removed = buffers_remove(&buflist, "b2");
  ASSERT(removed, "Expected existing buffer to have been removed");
  struct buffer *ne = buffers_find(&buflist, "b2");
  ASSERT(ne == NULL, "Expected to not find buffer after removal");
  ASSERT(buffers_num_buffers(&buflist) == 1,
         "Expected number of buffers to be 1 after removal.");

  ASSERT(hook_args.call_count == 1, "Expected remove hook to be called once.");
  ASSERT(
      hook_args.buffer[0] == bp2,
      "Expected remove hook to be called with the removed buffer as argument.");

  buffers_destroy(&buflist);
}

static void grow_list() {
  struct buffers buflist;
  buffers_init(&buflist, 2);

  struct buffer b1 = buffer_create("b1");
  struct buffer b2 = buffer_create("b2");
  struct buffer b3 = buffer_create("b3");
  struct buffer b4 = buffer_create("b4");

  struct buffer *bp1 = buffers_add(&buflist, b1);
  buffers_add(&buflist, b2);
  buffers_add(&buflist, b3);
  struct buffer *bp4 = buffers_add(&buflist, b4);
  ASSERT(buffers_num_buffers(&buflist) == 4,
         "Expected buffer count to be 4 after adding 4 buffers.");

  struct buffer *found = buffers_find(&buflist, "b1");
  ASSERT(found == bp1, "Expected pointer to remain stable after resize.");

  found = buffers_find(&buflist, "b4");
  ASSERT(found == bp4, "Expected to be able to find last added buffer.");

  buffers_destroy(&buflist);
}

void run_buflist_tests(void) {
  run_test(add);
  run_test(find);
  run_test(remove_buffer);
  run_test(grow_list);
}

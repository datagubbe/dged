#include "assert.h"
#include "stdlib.h"
#include "test.h"

#include "allocator.h"
#include "buffer.h"
#include "display.h"
#include "minibuffer.h"

static struct buffer b = {0};

static struct frame_allocator *g_alloc = NULL;

void *alloc_fn(size_t sz) { return frame_allocator_alloc(g_alloc, sz); }

void init() {
  if (b.name == NULL) {
    b = buffer_create("minibuffer", false);
  }

  minibuffer_init(&b);
}

void destroy() {
  if (b.name != NULL) {
    buffer_destroy(&b);
  }
}

void test_minibuffer_echo() {
  uint32_t relline, relcol;

  // TODO: how to clear this?
  struct frame_allocator alloc = frame_allocator_create(1024);
  g_alloc = &alloc;

  struct command_list *list =
      command_list_create(10, alloc_fn, 0, 0, "minibuffer");

  init();
  ASSERT(!minibuffer_displaying(),
         "Minibuffer should have nothing to display before echoing");

  minibuffer_echo("Test %s", "test");
  ASSERT(minibuffer_displaying(), "Minibuffer should now have text to display");

  minibuffer_clear();
  ASSERT(!minibuffer_displaying(),
         "Minibuffer should have nothing to display after clearing");

  minibuffer_echo_timeout(0, "You will not see me");
  buffer_update(&b, 100, 1, list, 0, &relline, &relcol);
  ASSERT(!minibuffer_displaying(),
         "A zero timeout echo should be cleared after first update");

  frame_allocator_destroy(&alloc);
  g_alloc = NULL;
  destroy();
}

int32_t fake(struct command_ctx ctx, int argc, const char *argv[]) { return 0; }

void test_minibuffer_prompt() {
  init();
  ASSERT(!minibuffer_focused(),
         "Minibuffer should not be focused without reason");

  struct command cmd = {
      .fn = fake,
      .name = "fake",
      .userdata = NULL,
  };
  struct command_ctx ctx = {.commands = NULL,
                            .active_window = NULL,
                            .buffers = NULL,
                            .userdata = NULL,
                            .self = &cmd};
  minibuffer_prompt(ctx, "prompt %s: ", "yes");

  ASSERT(minibuffer_focused(), "Minibuffer should get focused when prompting");

  minibuffer_abort_prompt();
  ASSERT(!minibuffer_focused(),
         "Minibuffer must not be focused after prompt has been aborted");
  destroy();
}

void run_minibuffer_tests() {
  run_test(test_minibuffer_echo);
  run_test(test_minibuffer_prompt);
}

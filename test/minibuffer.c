#include "assert.h"
#include "stdlib.h"
#include "test.h"

#include "dged/allocator.h"
#include "dged/buffer.h"
#include "dged/buffer_view.h"
#include "dged/buffers.h"
#include "dged/command.h"
#include "dged/display.h"
#include "dged/minibuffer.h"
#include "dged/settings.h"
#include "dged/timers.h"

static struct buffer b = {0};
static struct buffers bufs = {0};

static struct frame_allocator *g_alloc = NULL;

void *alloc_fn(size_t sz) { return frame_allocator_alloc(g_alloc, sz); }

void init() {
  if (b.name == NULL) {
    settings_init(10);
    timers_init();
    b = buffer_create("minibuffer");
    buffers_init(&bufs, 10);
  }

  minibuffer_init(&b, &bufs);
  windows_init(100, 100, &b, &b, &bufs);
}

void destroy() {
  if (b.name != NULL) {
    buffer_destroy(&b);
    buffers_destroy(&bufs);
    settings_destroy();
    timers_destroy();
    windows_destroy();
  }
}

void test_minibuffer_echo() {
  struct buffer_view view = buffer_view_create(&b, false, false);

  // TODO: how to clear this?
  struct frame_allocator alloc = frame_allocator_create(1024 * 1024);
  g_alloc = &alloc;

  struct command_list *list =
      command_list_create(10, alloc_fn, 0, 0, "minibuffer");

  init();
  ASSERT(!minibuffer_displaying(),
         "Minibuffer should have nothing to display before echoing");

  minibuffer_echo("Test %s", "test");
  ASSERT(minibuffer_displaying(), "Minibuffer should now have text to display");

  minibuffer_clear();
  struct buffer_view_update_params p = {
      .commands = list,
      .frame_alloc = alloc_fn,
      .window_id = -1,
      .frame_time = 0,
      .width = 100,
      .height = 1,
      .window_x = 0,
      .window_y = 25,
  };
  buffer_view_update(&view, &p);
  ASSERT(!minibuffer_displaying(),
         "Minibuffer should have nothing to display after clearing");

  minibuffer_echo_timeout(0, "You will not see me");

  buffer_view_update(&view, &p);
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

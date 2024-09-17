#include "frame-hooks.h"

#include "dged/hook.h"

HOOK_IMPL_NO_REMOVE(next_frame, next_frame_cb);

static next_frame_hook_vec g_next_frame_hooks;
static uint32_t g_next_frame_hook_id;

void init_frame_hooks(void) { VEC_INIT(&g_next_frame_hooks, 16); }

void teardown_frame_hooks(void) { VEC_DESTROY(&g_next_frame_hooks); }

void run_next_frame(next_frame_cb callback, void *userdata) {
  insert_next_frame_hook(&g_next_frame_hooks, &g_next_frame_hook_id, callback,
                         userdata);
}

size_t dispatch_next_frame_hooks() {
  size_t nhooks = VEC_SIZE(&g_next_frame_hooks);
  if (nhooks > 0) {
    dispatch_hook_no_args(&g_next_frame_hooks, struct next_frame_hook);
    VEC_CLEAR(&g_next_frame_hooks);
  }

  return nhooks;
}

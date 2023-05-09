#include "buffers.h"
#include "buffer.h"

#include <stdlib.h>

#include <string.h>

void buffers_init(struct buffers *buffers, uint32_t initial_capacity) {
  VEC_INIT(&buffers->buffers, initial_capacity);
  VEC_INIT(&buffers->add_hooks, 32);
}

struct buffer *buffers_add(struct buffers *buffers, struct buffer buffer) {
  VEC_PUSH(&buffers->buffers, buffer);

  struct buffer *slot = VEC_BACK(&buffers->buffers);
  VEC_FOR_EACH(&buffers->add_hooks, struct buffers_hook * hook) {
    hook->callback(slot, hook->userdata);
  }

  return slot;
}

uint32_t buffers_add_add_hook(struct buffers *buffers, buffers_hook_cb callback,
                              void *userdata) {
  VEC_PUSH(&buffers->add_hooks, ((struct buffers_hook){
                                    .callback = callback,
                                    .userdata = userdata,
                                }));

  return VEC_SIZE(&buffers->add_hooks) - 1;
}

struct buffer *buffers_find(struct buffers *buffers, const char *name) {
  VEC_FOR_EACH(&buffers->buffers, struct buffer * b) {
    if (strcmp(name, b->name) == 0) {
      return b;
    }
  }

  return NULL;
}

struct buffer *buffers_find_by_filename(struct buffers *buffers,
                                        const char *path) {
  VEC_FOR_EACH(&buffers->buffers, struct buffer * b) {
    if (b->filename != NULL && strcmp(path, b->filename) == 0) {
      return b;
    }
  }

  return NULL;
}

void buffers_for_each(struct buffers *buffers, buffers_hook_cb callback,
                      void *userdata) {
  VEC_FOR_EACH(&buffers->buffers, struct buffer * b) { callback(b, userdata); }
}

void buffers_destroy(struct buffers *buffers) {
  VEC_FOR_EACH(&buffers->buffers, struct buffer * b) { buffer_destroy(b); }

  VEC_DESTROY(&buffers->buffers);
  VEC_DESTROY(&buffers->add_hooks);
}

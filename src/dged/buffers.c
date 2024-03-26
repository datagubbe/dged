#include "buffers.h"
#include "buffer.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

struct buffer_entry {
  struct buffer buffer;
  bool empty;
};

void buffers_init(struct buffers *buffers, uint32_t initial_capacity) {
  VEC_INIT(&buffers->buffers, initial_capacity);
  VEC_INIT(&buffers->add_hooks, 32);
  VEC_INIT(&buffers->remove_hooks, 32);
}

struct buffer *buffers_add(struct buffers *buffers, struct buffer buffer) {
  struct buffer_entry *slot = NULL;
  VEC_FOR_EACH(&buffers->buffers, struct buffer_entry * e) {
    if (e->empty) {
      slot = e;
    }
  }

  if (slot == NULL) {
    VEC_APPEND(&buffers->buffers, struct buffer_entry * new);
    slot = new;
  }

  slot->buffer = buffer;
  slot->empty = false;

  VEC_FOR_EACH(&buffers->add_hooks, struct buffers_hook * hook) {
    hook->callback(&slot->buffer, hook->userdata);
  }

  return &slot->buffer;
}

uint32_t buffers_add_add_hook(struct buffers *buffers, buffers_hook_cb callback,
                              void *userdata) {
  VEC_PUSH(&buffers->add_hooks, ((struct buffers_hook){
                                    .callback = callback,
                                    .userdata = userdata,
                                }));

  return VEC_SIZE(&buffers->add_hooks) - 1;
}

uint32_t buffers_add_remove_hook(struct buffers *buffers,
                                 buffers_hook_cb callback, void *userdata) {
  VEC_PUSH(&buffers->remove_hooks, ((struct buffers_hook){
                                       .callback = callback,
                                       .userdata = userdata,
                                   }));

  return VEC_SIZE(&buffers->remove_hooks) - 1;
}

struct buffer *buffers_find(struct buffers *buffers, const char *name) {
  VEC_FOR_EACH(&buffers->buffers, struct buffer_entry * e) {
    if (!e->empty && strcmp(name, e->buffer.name) == 0) {
      return &e->buffer;
    }
  }

  return NULL;
}

struct buffer *buffers_find_by_filename(struct buffers *buffers,
                                        const char *path) {
  VEC_FOR_EACH(&buffers->buffers, struct buffer_entry * e) {
    if (!e->empty && e->buffer.filename != NULL &&
        strcmp(path, e->buffer.filename) == 0) {
      return &e->buffer;
    }
  }

  return NULL;
}

bool buffers_remove(struct buffers *buffers, const char *name) {
  struct buffer_entry *buf_entry = NULL;
  VEC_FOR_EACH(&buffers->buffers, struct buffer_entry * e) {
    if (!e->empty && strcmp(name, e->buffer.name) == 0) {
      buf_entry = e;
    }
  }

  if (buf_entry == NULL) {
    return false;
  }

  VEC_FOR_EACH(&buffers->remove_hooks, struct buffers_hook * hook) {
    hook->callback(&buf_entry->buffer, hook->userdata);
  }

  buf_entry->empty = true;
  buffer_destroy(&buf_entry->buffer);
  return true;
}

void buffers_for_each(struct buffers *buffers, buffers_hook_cb callback,
                      void *userdata) {
  VEC_FOR_EACH(&buffers->buffers, struct buffer_entry * e) {
    if (!e->empty) {
      callback(&e->buffer, userdata);
    }
  }
}

uint32_t buffers_num_buffers(struct buffers *buffers) {
  return VEC_SIZE(&buffers->buffers);
}

struct buffer *buffers_first(struct buffers *buffers) {
  return buffers_num_buffers(buffers) > 0
             ? &VEC_ENTRIES(&buffers->buffers)[0].buffer
             : NULL;
}

void buffers_destroy(struct buffers *buffers) {
  VEC_FOR_EACH(&buffers->buffers, struct buffer_entry * e) {
    if (!e->empty) {
      buffer_destroy(&e->buffer);
      e->empty = true;
    }
  }

  VEC_DESTROY(&buffers->buffers);
  VEC_DESTROY(&buffers->add_hooks);
  VEC_DESTROY(&buffers->remove_hooks);
}

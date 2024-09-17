#include "buffers.h"
#include "buffer.h"
#include "s8.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static struct buffer_chunk *new_chunk(uint32_t sz) {
  struct buffer_chunk *chunk = calloc(1, sizeof(struct buffer_chunk));
  chunk->entries = calloc(sz, sizeof(struct buffer_entry));
  return chunk;
}

static bool chunk_empty(const struct buffer_chunk *chunk, uint32_t sz) {
  for (uint32_t i = 0; i < sz; ++i) {
    if (chunk->entries[i].occupied) {
      return false;
    }
  }

  return true;
}

static void free_chunk(struct buffer_chunk *chunk) {
  free(chunk->entries);
  chunk->entries = NULL;
  chunk->next = NULL;
  free(chunk);
}

void buffers_init(struct buffers *buffers, uint32_t initial_capacity) {
  buffers->chunk_size = initial_capacity;
  buffers->head = new_chunk(buffers->chunk_size);
  VEC_INIT(&buffers->add_hooks, 32);
  VEC_INIT(&buffers->remove_hooks, 32);
}

struct buffer *buffers_add(struct buffers *buffers, struct buffer buffer) {
  struct buffer_entry *slot = NULL;
  struct buffer_chunk *chunk = buffers->head, *prev_chunk = buffers->head;
  while (chunk != NULL) {
    for (uint32_t i = 0; i < buffers->chunk_size; ++i) {
      if (!chunk->entries[i].occupied) {
        slot = &chunk->entries[i];
        goto found;
      }
    }

    prev_chunk = chunk;
    chunk = chunk->next;
  }

  chunk = new_chunk(buffers->chunk_size);
  prev_chunk->next = chunk;

  slot = &chunk->entries[0];

found:

  slot->buffer = buffer;
  slot->occupied = true;

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
  struct buffer_chunk *chunk = buffers->head;
  size_t namelen = strlen(name);
  while (chunk != NULL) {
    for (uint32_t i = 0; i < buffers->chunk_size; ++i) {
      if (!chunk->entries[i].occupied) {
        continue;
      }

      struct buffer *b = &chunk->entries[i].buffer;
      size_t bnamelen = b->name != NULL ? strlen(b->name) : 0;
      if (namelen == bnamelen && memcmp(name, b->name, bnamelen) == 0) {
        return b;
      }
    }

    chunk = chunk->next;
  }

  return NULL;
}

struct buffer *buffers_find_by_filename(struct buffers *buffers,
                                        const char *path) {
  struct buffer_chunk *chunk = buffers->head;
  struct s8 needle = s8(path);
  while (chunk != NULL) {
    for (uint32_t i = 0; i < buffers->chunk_size; ++i) {
      if (!chunk->entries[i].occupied) {
        continue;
      }

      struct buffer *b = &chunk->entries[i].buffer;
      if (b->filename == NULL) {
        continue;
      }

      struct s8 bname = s8(b->filename);
      if (s8endswith(bname, needle)) {
        return b;
      }
    }

    chunk = chunk->next;
  }

  return NULL;
}

bool buffers_remove(struct buffers *buffers, const char *name) {
  struct buffer_chunk *chunk = buffers->head, *prev_chunk = buffers->head;
  struct buffer_entry *buf_entry = NULL;
  size_t namelen = strlen(name);
  while (chunk != NULL) {
    for (uint32_t i = 0; i < buffers->chunk_size; ++i) {
      struct buffer *b = &chunk->entries[i].buffer;
      size_t bnamelen = strlen(b->name);
      if (chunk->entries[i].occupied && namelen == bnamelen &&
          memcmp(name, b->name, bnamelen) == 0) {
        buf_entry = &chunk->entries[i];
        goto found;
      }
    }

    prev_chunk = chunk;
    chunk = chunk->next;
  }

  return false;

found:

  VEC_FOR_EACH(&buffers->remove_hooks, struct buffers_hook * hook) {
    hook->callback(&buf_entry->buffer, hook->userdata);
  }

  buf_entry->occupied = false;
  buffer_destroy(&buf_entry->buffer);

  if (chunk_empty(chunk, buffers->chunk_size) && chunk != buffers->head) {
    prev_chunk->next = chunk->next;
    free_chunk(chunk);
  }
  return true;
}

void buffers_for_each(struct buffers *buffers, buffers_hook_cb callback,
                      void *userdata) {
  struct buffer_chunk *chunk = buffers->head;
  while (chunk != NULL) {
    for (uint32_t i = 0; i < buffers->chunk_size; ++i) {
      if (chunk->entries[i].occupied) {
        callback(&chunk->entries[i].buffer, userdata);
      }
    }

    chunk = chunk->next;
  }
}

uint32_t buffers_num_buffers(struct buffers *buffers) {
  uint32_t total_buffers = 0;
  struct buffer_chunk *chunk = buffers->head;
  while (chunk != NULL) {
    for (uint32_t i = 0; i < buffers->chunk_size; ++i) {
      if (chunk->entries[i].occupied) {
        ++total_buffers;
      }
    }

    chunk = chunk->next;
  }

  return total_buffers;
}

struct buffer *buffers_first(struct buffers *buffers) {
  return buffers_num_buffers(buffers) > 0 ? &buffers->head->entries[0].buffer
                                          : NULL;
}

void buffers_destroy(struct buffers *buffers) {
  struct buffer_chunk *chunk = buffers->head;
  while (chunk != NULL) {
    for (uint32_t i = 0; i < buffers->chunk_size; ++i) {
      if (chunk->entries[i].occupied) {
        buffer_destroy(&chunk->entries[i].buffer);
        chunk->entries[i].occupied = false;
      }
    }

    struct buffer_chunk *old = chunk;
    chunk = chunk->next;
    free_chunk(old);
  }

  VEC_DESTROY(&buffers->add_hooks);
  VEC_DESTROY(&buffers->remove_hooks);
}

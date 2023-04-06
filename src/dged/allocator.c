#include "allocator.h"

struct frame_allocator frame_allocator_create(size_t capacity) {
  return (struct frame_allocator){
      .capacity = capacity, .offset = 0, .buf = (uint8_t *)malloc(capacity)};
}

void frame_allocator_destroy(struct frame_allocator *alloc) {
  free(alloc->buf);
  alloc->buf = NULL;
}

void *frame_allocator_alloc(struct frame_allocator *alloc, size_t sz) {
  if (alloc->offset + sz > alloc->capacity) {
    return NULL;
  }

  void *mem = alloc->buf + alloc->offset;
  alloc->offset += sz;

  return mem;
}

void frame_allocator_clear(struct frame_allocator *alloc) { alloc->offset = 0; }

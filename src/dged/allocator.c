#include "allocator.h"

#include <stdlib.h>
#include <sys/mman.h>

struct frame_allocator frame_allocator_create(size_t capacity) {
  void *mem = mmap(NULL, capacity, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  if (mem == MAP_FAILED) {
    return (struct frame_allocator){.capacity = 0, .offset = 0, .buf = NULL};
  }

  return (struct frame_allocator){
      .capacity = capacity, .offset = 0, .buf = mem};
}

void frame_allocator_destroy(struct frame_allocator *alloc) {
  munmap(alloc->buf, alloc->capacity);
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

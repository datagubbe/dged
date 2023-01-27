#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/**
 * Simple bump allocator that can be used for
 * allocations with a frame lifetime.
 */
struct frame_allocator {
  uint8_t *buf;
  size_t offset;
  size_t capacity;
};

/**
 * Create a new frame allocator
 *
 * @param capacity The capacity in bytes of the frame allocator
 * @returns The frame allocator
 */
struct frame_allocator frame_allocator_create(size_t capacity);

/**
 * Destroy a frame allocator.
 *
 * @param alloc The @ref frame_allocator "frame allocator" to destroy.
 */
void frame_allocator_destroy(struct frame_allocator *alloc);

/**
 * Allocate memory in this @ref frame_allocator "frame allocator"
 *
 * @param alloc The allocator to allocate in
 * @param sz The size in bytes to allocate.
 * @returns void* representing the start of the allocated region on success,
 * NULL on failure.
 */
void *frame_allocator_alloc(struct frame_allocator *alloc, size_t sz);

/**
 * Clear this @ref frame_allocator "frame allocator".
 *
 * This does not free any memory, but simply resets the offset to 0.
 * @param alloc The frame allocator to clear
 */
void frame_allocator_clear(struct frame_allocator *alloc);

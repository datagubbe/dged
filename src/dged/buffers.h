#ifndef _BUFFERS_H
#define _BUFFERS_H

#include "buffer.h"
#include "vec.h"

#include <stdbool.h>
#include <stdint.h>

typedef void (*buffers_hook_cb)(struct buffer *buffer, void *userdata);

struct buffers_hook {
  buffers_hook_cb callback;
  void *userdata;
};

/**
 * An entry in a buffer list.
 */
struct buffer_entry {
  /** Storage for the actual buffer. */
  struct buffer buffer;

  /** False if this entry is free to use. */
  bool occupied;
};

struct buffer_chunk {
  struct buffer_entry *entries;
  struct buffer_chunk *next;
};

/**
 * A buffer list.
 */
struct buffers {
  struct buffer_chunk *head;
  uint32_t chunk_size;
  VEC(struct buffers_hook) add_hooks;
  VEC(struct buffers_hook) remove_hooks;
};

/**
 * Initialize a buffer list.
 *
 * @param [in] buffers The buffer list to initialize.
 * @param [in] initial_capacity The initial number of buffers
               that this buffer list should be able to hold.
 */
void buffers_init(struct buffers *buffers, uint32_t initial_capacity);

/**
 * Destroy a buffer list.
 *
 * This will free any memory associated with the list and all
 * associated buffer pointers will be invalid after this.
 *
 * @param [in] buffers The buffer list to destroy.
 */
void buffers_destroy(struct buffers *buffers);

/**
 * Add a buffer to the list.
 *
 * @param [in] buffers The buffer list to add to.
 * @param [in] buffer The buffer to add to the list.
 *
 * @returns A stable pointer to the buffer in the buffer list.
 *          This pointer do not change when the buffer list resizes.
 */
struct buffer *buffers_add(struct buffers *buffers, struct buffer buffer);

/**
 * Find a buffer using its name.
 *
 * @param [in] buffers The buffer list to search in.
 * @param [in] name The buffer name to search from.
 *
 * @returns A stable pointer to the buffer in the buffer list.
 *          This pointer do not change when the buffer list resizes.
 *          If not found, NULL is returned.
 */
struct buffer *buffers_find(struct buffers *buffers, const char *name);

/**
 * Find a buffer using its filename.
 *
 * @param [in] buffers The buffer list to search in.
 * @param [in] name The buffer filename to search from.
 *
 * @returns A stable pointer to the buffer in the buffer list.
 *          This pointer do not change when the buffer list resizes.
 *          If not found, NULL is returned.
 */
struct buffer *buffers_find_by_filename(struct buffers *buffers,
                                        const char *path);

/**
 * Remove a buffer from the buffer list.
 *
 * @param [in] buffers The buffer list to remove from.
 * @param [in] name The buffer name to remove.
 *
 * @returns True if the buffer was found and removed.
 */
bool buffers_remove(struct buffers *buffers, const char *name);

/**
 * Add a hook for when buffers are added to the buffer list.
 *
 * @param [in] buffers The buffer list to add hook to.
 * @param [in] callback The callback to call when a buffer is added.
 * @param [in] userdata Pointer to userdata that is passed unmodified to the
 * callback.
 *
 * @returns A handle to the hook.
 */
uint32_t buffers_add_add_hook(struct buffers *buffers, buffers_hook_cb callback,
                              void *userdata);

/**
 * Add a hook for when buffers are removed from the buffer list.
 *
 * @param [in] buffers The buffer list to add hook to.
 * @param [in] callback The callback to call when a buffer is removed.
 * @param [in] userdata Pointer to userdata that is passed unmodified to the
 * callback.
 *
 * @returns A handle to the hook.
 */
uint32_t buffers_add_remove_hook(struct buffers *buffers,
                                 buffers_hook_cb callback, void *userdata);

/**
 * Iterate the buffers in a buffer list.
 *
 * @param [in] buffers The buffer list to iterate.
 * @param [in] callback The callback to call for each buffer in `buffers`.
 * @param [in] userdata Pointer to userdata that is passed unmodified to the
 * callback.
 */
void buffers_for_each(struct buffers *buffers, buffers_hook_cb callback,
                      void *userdata);

/**
 * Number of buffers in the buffer list.
 *
 * @param [in] buffers The buffer list to iterate.
 * @returns The number of buffers in the buffer list.
 */
uint32_t buffers_num_buffers(struct buffers *buffers);

/**
 * Get the first buffer in the buffer list.
 *
 * @param [in] buffers The buffer list.
 * @returns A stable pointer to the first buffer
            in `buffers`.
 */
struct buffer *buffers_first(struct buffers *buffers);

#endif

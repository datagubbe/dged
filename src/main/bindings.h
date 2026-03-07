#ifndef _BINDINGS_H
#define _BINDINGS_H

#include <stdint.h>

#include "dged/hook.h"

struct keymap;
struct buffer;
struct binding;

/**
 * Intialize all structures for bindings and set
 * default bindings up.
 */
void init_bindings(void);

/**
 * Get the default keymap for buffers.
 *
 * @returns The default buffer keymap.
 */
struct keymap *buffer_default_keymap(void);

/** Id for a keymap currently added to a buffer. */
typedef uint64_t buffer_keymap_id;

/**
 * Add a keymap to a buffer.
 *
 * @param [in] buffer The buffer to add the keymap to.
 * @param [in] keymap The keymap to add.
 *
 * @returns A keymap id that can be used to refer to this keymap-buffer
 *          combination.
 */
buffer_keymap_id buffer_add_keymap(struct buffer *buffer, struct keymap keymap);

/**
 * Remove a keymap for a buffer.
 *
 * @param [in] id The id for the buffer keymap to remove.
 */
void buffer_remove_keymap(buffer_keymap_id id);

/**
 * Retrieve keymaps for a buffer.
 *
 * @param [in] buffer The buffer to get applicable keymaps for.
 * @param [in] keymaps An array to store keymaps in.
 * @param [in] The capacity of @ref keymaps.
 * @returns The number of keymaps stored in @ref keymaps.
 */
uint32_t buffer_keymaps(struct buffer *buffer, struct keymap *keymaps[],
                        uint32_t max_nkeymaps);

/**
 * Remove all keymaps for a buffer.
 *
 * @param [in] buffer The buffer to remove keymaps for.
 */
void buffer_remove_keymaps(struct buffer *buffer);

/** Callback for a buffer keymaps hook */
typedef uint32_t (*buffer_keymaps_cb)(struct buffer *, struct keymap **,
                                      uint32_t, void *);

/**
 * Add a hook for buffer keymaps
 *
 * This makes it possible to programatically append to the list that
 * is returned by @ref buffer_keymaps.
 *
 * @param [in] callback The callback function to call.
 * @param [in] userdata The userdata to pass to the callback function.
 *
 @ @returns The hook id.
 */
uint32_t buffer_add_keymaps_hook(buffer_keymaps_cb callback, void *userdata);

/**
 * Remove a previously added hook for buffer keymaps.
 *
 * @param [in] id The id of the hook to remove.
 * @param [in] callback A callback function called with the userdata of the
 *             original hook for doing cleanup.
 */
void buffer_remove_keymaps_hook(uint32_t id, remove_hook_cb callback);

/**
 * Tear down the structures for bindings.
 *
 * Symmetric to @ref init_bindings.
 */
void destroy_bindings(void);

#endif

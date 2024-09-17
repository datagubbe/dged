#include <stdint.h>

#include "dged/hook.h"

struct keymap;
struct buffer;
struct binding;

void init_bindings(void);

struct keymap *buffer_default_keymap(void);

typedef uint64_t buffer_keymap_id;
buffer_keymap_id buffer_add_keymap(struct buffer *buffer, struct keymap keymap);
void buffer_remove_keymap(buffer_keymap_id id);
uint32_t buffer_keymaps(struct buffer *buffer, struct keymap *keymaps[],
                        uint32_t max_nkeymaps);

typedef uint32_t (*buffer_keymaps_cb)(struct buffer *, struct keymap **,
                                      uint32_t, void *);
uint32_t buffer_add_keymaps_hook(buffer_keymaps_cb callback, void *userdata);
void buffer_remove_keymaps_hook(uint32_t id, remove_hook_cb callback);

void destroy_bindings(void);

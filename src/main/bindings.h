#include <stdint.h>

struct keymap;
struct buffer;
struct binding;

void init_bindings();

typedef uint64_t buffer_keymap_id;
buffer_keymap_id buffer_add_keymap(struct buffer *buffer, struct keymap keymap);
void buffer_remove_keymap(buffer_keymap_id id);
uint32_t buffer_keymaps(struct buffer *buffer, struct keymap *keymaps[],
                        uint32_t max_nkeymaps);

void destroy_bindings();

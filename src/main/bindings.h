#include <stdint.h>

struct keymap;
struct buffer;
struct binding;

struct keymap *register_bindings();

void buffer_bind_keys(struct buffer *buffer, struct binding *bindings,
                      uint32_t nbindings);
void reset_buffer_keys(struct buffer *buffer);
void reset_minibuffer_keys(struct buffer *minibuffer);
struct keymap *buffer_keymap(struct buffer *buffer);

void destroy_keymaps();

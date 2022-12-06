#include "keyboard.h"

struct keymap {
  const char *name;
  struct binding *bindings;
  uint32_t nbindings;
  uint32_t capacity;
};

enum binding_type { BindingType_Command, BindingType_Keymap };

#define BINDING(mod_, c_, command_)                                            \
  (struct binding) {                                                           \
    .key = {.mod = mod_, .c = c_}, .type = BindingType_Command,                \
    .command = hash_command_name(command_)                                     \
  }

#define PREFIX(mod_, c_, keymap_)                                              \
  (struct binding) {                                                           \
    .key = {.mod = mod_, .c = c_}, .type = BindingType_Keymap,                 \
    .keymap = keymap_                                                          \
  }

struct binding {
  struct key key;

  uint8_t type;

  union {
    uint32_t command;
    struct keymap *keymap;
  };
};

struct lookup_result {
  bool found;
  uint8_t type;
  union {
    struct command *command;
    struct keymap *keymap;
  };
};

struct commands;

struct keymap keymap_create(const char *name, uint32_t capacity);
void keymap_bind_keys(struct keymap *keymap, struct binding *bindings,
                      uint32_t nbindings);
void keymap_destroy(struct keymap *keymap);

struct lookup_result lookup_key(struct keymap *keymaps, uint32_t nkeymaps,
                                struct key *key, struct commands *commands);

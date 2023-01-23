#include "keyboard.h"

struct keymap {
  const char *name;
  struct binding *bindings;
  uint32_t nbindings;
  uint32_t capacity;
};

enum binding_type {
  BindingType_Command,
  BindingType_Keymap,
  BindingType_DirectCommand
};

#define BINDING_INNER(mod_, c_, command_)                                      \
  (struct binding) {                                                           \
    .key = {.mod = mod_, .key = c_}, .type = BindingType_Command,              \
    .command = hash_command_name(command_)                                     \
  }

#define ANONYMOUS_BINDING_INNER(mod_, c_, command_)                            \
  (struct binding) {                                                           \
    .key = {.mod = mod_, .key = c_}, .type = BindingType_DirectCommand,        \
    .direct_command = command_                                                 \
  }

#define PREFIX_INNER(mod_, c_, keymap_)                                        \
  (struct binding) {                                                           \
    .key = {.mod = mod_, .key = c_}, .type = BindingType_Keymap,               \
    .keymap = keymap_                                                          \
  }

#define BINDING(...) BINDING_INNER(__VA_ARGS__)
#define PREFIX(...) PREFIX_INNER(__VA_ARGS__)
#define ANONYMOUS_BINDING(...) ANONYMOUS_BINDING_INNER(__VA_ARGS__)

struct binding {
  struct key key;

  uint8_t type;

  union {
    uint32_t command;
    struct command *direct_command;
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

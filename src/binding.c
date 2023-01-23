#include "binding.h"
#include "command.h"

#include <stdlib.h>
#include <string.h>

struct keymap keymap_create(const char *name, uint32_t capacity) {
  return (struct keymap){
      .name = name,
      .bindings = calloc(capacity, sizeof(struct binding)),
      .nbindings = 0,
      .capacity = capacity,
  };
}

void keymap_bind_keys(struct keymap *keymap, struct binding *bindings,
                      uint32_t nbindings) {
  if (keymap->nbindings + nbindings >= keymap->capacity) {
    keymap->capacity =
        nbindings > keymap->capacity * 2 ? nbindings * 2 : keymap->capacity * 2;
    keymap->bindings =
        realloc(keymap->bindings, sizeof(struct binding) * keymap->capacity);
  }
  memcpy(keymap->bindings + keymap->nbindings, bindings,
         sizeof(struct binding) * nbindings);

  keymap->nbindings += nbindings;
}

void keymap_destroy(struct keymap *keymap) {
  free(keymap->bindings);
  keymap->bindings = 0;
  keymap->capacity = 0;
  keymap->nbindings = 0;
}

struct lookup_result lookup_key(struct keymap *keymaps, uint32_t nkeymaps,
                                struct key *key, struct commands *commands) {
  // lookup in reverse order in the keymaps
  uint32_t kmi = nkeymaps;
  while (kmi > 0) {
    --kmi;
    struct keymap *keymap = &keymaps[kmi];

    for (uint32_t bi = 0; bi < keymap->nbindings; ++bi) {
      struct binding *binding = &keymap->bindings[bi];
      if (key_equal(key, &binding->key)) {
        switch (binding->type) {
        case BindingType_Command: {
          return (struct lookup_result){
              .found = true,
              .type = BindingType_Command,
              .command = lookup_command_by_hash(commands, binding->command),
          };
        }
        case BindingType_Keymap: {
          return (struct lookup_result){
              .found = true,
              .type = BindingType_Keymap,
              .keymap = binding->keymap,
          };
        }
        case BindingType_DirectCommand:
          return (struct lookup_result){
              .found = true,
              .type = BindingType_Command,
              .command = binding->direct_command,
          };
        }
      }
    }
  }

  return (struct lookup_result){.found = false};
}

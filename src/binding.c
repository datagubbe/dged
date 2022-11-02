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

struct command *lookup_key(struct keymap *keymaps, uint32_t nkeymaps,
                           struct key *key, struct commands *commands) {
  // lookup in order in the keymaps
  for (uint32_t kmi = 0; kmi < nkeymaps; ++kmi) {
    struct keymap *keymap = &keymaps[kmi];

    for (uint32_t bi = 0; bi < keymap->nbindings; ++bi) {
      struct binding *binding = &keymap->bindings[bi];
      if (key->c == binding->key.c && key->mod == binding->key.mod) {
        if (binding->type == BindingType_Command) {
          return lookup_command_by_hash(commands, binding->command);
        } else if (binding->type == BindingType_Keymap) {
          // TODO
          return NULL;
        }
      }
    }
  }

  return NULL;
}

#include "hash.h"
#include "keyboard.h"

/**
 * Directory of keyboard mappings.
 */
struct keymap {
  /** Keymap name */
  const char *name;

  /** The bindings in this keymap */
  struct binding *bindings;

  /** The number of bindings in this keymap */
  uint32_t nbindings;

  /** The number of bindings this keymap can currently hold */
  uint32_t capacity;
};

/**
 * Type of a keyboard binding
 */
enum binding_type {
  /** This binding is to a command */
  BindingType_Command,

  /** This binding is to another keymap */
  BindingType_Keymap,

  /** This binding is to an already resolved command,
   * a.k.a. anonymous binding.
   */
  BindingType_DirectCommand
};

#define BINDING_INNER(mod_, c_, command_)                                      \
  (struct binding) {                                                           \
    .key = {.mod = mod_, .key = c_}, .type = BindingType_Command,              \
    .data.command = hash_name(command_)                                        \
  }

#define ANONYMOUS_BINDING_INNER(mod_, c_, command_)                            \
  (struct binding) {                                                           \
    .key = {.mod = mod_, .key = c_}, .type = BindingType_DirectCommand,        \
    .data.direct_command = command_                                            \
  }

#define PREFIX_INNER(mod_, c_, keymap_)                                        \
  (struct binding) {                                                           \
    .key = {.mod = mod_, .key = c_}, .type = BindingType_Keymap,               \
    .data.keymap = keymap_                                                     \
  }

/**
 * Define a key binding.
 *
 * Used like BINDING((mod), key, "<command-name>") where
 * the key modifier is optional.
 */
#define BINDING(...) BINDING_INNER(__VA_ARGS__)

/**
 * Define a prefix (a keybinding for a keymap).
 *
 * Used like PREFIX((mod), key, <ptr-to-keymap>) where
 * the key modifier is optional.
 */
#define PREFIX(...) PREFIX_INNER(__VA_ARGS__)

/**
 * Define an anonymous binding, i.e. a binding directly to a function.
 *
 * Note the function that this key binds to cannot usually be
 * executed dynamically (with M-x).
 *
 * Used like ANONYMOUS_BINDING((mod), key, <ptr-to-command>) where
 * the key modifier is optional.
 */
#define ANONYMOUS_BINDING(...) ANONYMOUS_BINDING_INNER(__VA_ARGS__)

/**
 * A keyboard key bound to an action
 */
struct binding {
  /** The keyboard key that triggers the action in this binding */
  struct key key;

  /** Type of this binding, see @ref binding_type */
  uint8_t type;

  union binding_data {
    /** A hash of a command name */
    uint32_t command;
    /** A command */
    struct command *direct_command;
    /** A keymap */
    struct keymap *keymap;
  } data;
};

/**
 * Result of a binding lookup
 */
struct lookup_result {
  /** True if a binding was found */
  bool found;

  /** Type of binding in the result */
  uint8_t type;

  union lookup_data {
    /** A command */
    struct command *command;
    /** A keymap */
    struct keymap *keymap;
  } data;
};

struct commands;

/**
 * Create a new keymap
 *
 * @param name Name of the keymap
 * @param capacity Initial capacity of the keymap.
 * @returns The created keymap
 */
struct keymap keymap_create(const char *name, uint32_t capacity);

/**
 * Bind keys in a keymap.
 *
 * @param keymap The keymap to bind keys in.
 * @param bindings Bindings to add.
 * @param nbindings Number of bindings in @p bindings.
 */
void keymap_bind_keys(struct keymap *keymap, struct binding *bindings,
                      uint32_t nbindings);

/**
 * Destroy a keymap.
 *
 * This clears all keybindings associated with the keymap.
 * @param keymap Keymap to destroy.
 */
void keymap_destroy(struct keymap *keymap);

/**
 * Lookup the binding for a key in a set of keymaps.
 *
 * @param keymaps The keymaps to look in.
 * @param nkeymaps The number of keymaps in @p keymaps.
 * @param key The keystroke to look up bindings for.
 * @param commands Available commands for lookup.
 * @returns A @ref lookup_result with the result of the lookup.
 */
struct lookup_result lookup_key(struct keymap *keymaps, uint32_t nkeymaps,
                                struct key *key, struct commands *commands);

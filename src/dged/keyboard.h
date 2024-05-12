#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Key press modifiers
 *
 * Modifiers a key press can have.
 */
enum modifiers {
  /** No modifier, bare key press */
  None = 0,

  /** Ctrl key */
  Ctrl = 1 << 0,

  /** Meta (Alt) key */
  Meta = 1 << 1,

  /** Special key (F keys, arrow keys, etc) */
  Spec = 1 << 2,
};

/** Backspace key */
#define BACKSPACE Ctrl, '?'
/** Tab key */
#define TAB Ctrl, 'I'
/** Enter key */
#define ENTER Ctrl, 'M'
/** Delete key */
#define DELETE Spec, '3'

/** Up arrow key */
#define UP Spec, 'A'
/** Down arrow key */
#define DOWN Spec, 'B'
/** Right arrow key */
#define RIGHT Spec, 'C'
/** Left arrow key */
#define LEFT Spec, 'D'

/**
 * A key press
 */
struct key {
  /** The key pressed, will be 0 for a unicode char */
  uint8_t key;
  /** Modifier keys pressed (or-ed together) */
  uint8_t mod;
  /** Index where this key press starts in the raw input buffer */
  uint32_t start;
  /** Index where this key press ends in the raw input buffer */
  uint32_t end;
};

/**
 * The keyboard used to input characters.
 */
struct keyboard {
  uint32_t reactor_event_id;
  int fd;
};

/**
 * The result of updating the keyboard
 */
struct keyboard_update {
  /** The key presses */
  struct key *keys;
  /** Number of key presses in @ref keys */
  uint32_t nkeys;

  /** The raw input */
  uint8_t *raw;
  /** The number of bytes in the raw input */
  uint32_t nbytes;
};

struct reactor;

/**
 * Create a new keyboard
 *
 * @param reactor @ref reactor "Reactor" to use for polling keyboard for
 * readiness.
 * @returns The created keyboard.
 */
struct keyboard keyboard_create(struct reactor *reactor);

/**
 * Create a new keyboard, reading input from fd
 *
 * @param reactor @ref reactor "Reactor" to use for polling keyboard for
 * readiness.
 * @param fd The file descriptor to get input from
 * @returns The created keyboard.
 */
struct keyboard keyboard_create_fd(struct reactor *reactor, int fd);

/**
 * Update the keyboard.
 *
 * This will check the reactor for readiness to avoid blocking. If there is
 * data, it will be read and converted to key presses.
 *
 * @param kbd The @ref keyboard to update.
 * @param reactor The @ref reactor used when creating the @ref keyboard.
 * @param frame_alloc Allocation function to use for creating the keyboard
 * update buffer.
 * @returns An instance of @ref keyboard_update representing the result of the
 * update operation.
 */
struct keyboard_update keyboard_update(struct keyboard *kbd,
                                       struct reactor *reactor,
                                       void *(*frame_alloc)(size_t));

/**
 * Does key represent the same key press as mod and c.
 *
 * @param key The key to check.
 * @param mod Modifier of a key to compare against.
 * @param c Char of a key to compare against.
 * @returns true if key represents the same key press as mod together with c,
 * false otherwise
 */
bool key_equal_char(struct key *key, uint8_t mod, uint8_t c);

/**
 * Does key1 represent the same key press as key2?
 *
 * @param key1 First key to compare.
 * @param key2 Second key to compare.
 * @returns true if key1 and key2 represents the same key press, false
 * otherwise.
 */
bool key_equal(struct key *key1, struct key *key2);

/**
 * Get a text representation of a key
 *
 * @param key @ref key "Key" to get text representation for.
 * @param buf character buffer for holding the result.
 * @param capacity The capacity of buf.
 *
 * @returns The number of characters written to buf.
 */
uint32_t key_name(struct key *key, char *buf, size_t capacity);

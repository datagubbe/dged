#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum modifiers {
  None = 0,
  Ctrl = 1 << 0,
  Meta = 1 << 1,
  Spec = 1 << 2,
};

#define BACKSPACE Ctrl, '?'
#define DELETE Spec, '3'

#define UP Spec, 'A'
#define DOWN Spec, 'B'
#define RIGHT Spec, 'C'
#define LEFT Spec, 'D'

struct key {
  uint8_t key;
  uint8_t mod;
  uint8_t start;
  uint8_t end;
};

struct keyboard {
  uint32_t reactor_event_id;
  bool has_data;
};

struct keyboard_update {
  struct key keys[32];
  uint32_t nkeys;

  uint8_t raw[64];
  uint32_t nbytes;
};

struct reactor;

struct keyboard keyboard_create(struct reactor *reactor);

struct keyboard_update keyboard_update(struct keyboard *kbd,
                                       struct reactor *reactor);

bool key_equal_char(struct key *key, uint8_t mod, uint8_t c);
bool key_equal(struct key *key1, struct key *key2);
void key_name(struct key *key, char *buf, size_t capacity);

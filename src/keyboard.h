#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum modifiers {
  Ctrl = 1 << 0,
  Meta = 1 << 1,
};

// note that unicode chars are split over multiple keypresses
// TODO: make unicode chars nicer to deal with
struct key {
  uint8_t c;
  uint8_t mod;
};

struct keyboard {
  uint32_t reactor_event_id;
  bool has_data;
};

struct keyboard_update {
  struct key keys[32];
  uint32_t nkeys;
};

struct reactor;

struct keyboard keyboard_create(struct reactor *reactor);

struct keyboard_update keyboard_update(struct keyboard *kbd,
                                       struct reactor *reactor);

bool key_equal(struct key *key, uint8_t mod, uint8_t c);
void key_name(struct key *key, char *buf, size_t capacity);

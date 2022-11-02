#include <stdbool.h>
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

struct keyboard {};

struct keyboard_update {
  struct key keys[32];
  uint32_t nkeys;
};

struct keyboard keyboard_create();

struct keyboard_update keyboard_begin_frame(struct keyboard *kbd);
void keyboard_end_frame(struct keyboard *kbd);

bool key_equal(struct key *key, uint8_t mod, uint8_t c);

#include "keyboard.h"

#include <string.h>
#include <unistd.h>

struct keyboard keyboard_create() {
  // TODO: should input term stuff be set here?
  return (struct keyboard){};
}

void parse_keys(uint8_t *bytes, uint32_t nbytes, struct key *out_keys,
                uint32_t *out_nkeys) {
  uint32_t nkps = 0;
  for (uint32_t bytei = 0; bytei < nbytes; ++bytei) {
    uint8_t b = bytes[bytei];

    struct key *kp = &out_keys[nkps];

    if (b == 0x1b) { // meta
      kp->mod |= Meta;
    } else if (b >= 0x00 && b <= 0x1f) { // ctrl char
      kp->mod |= Ctrl;
      kp->c = b | 0x40;

    } else if (b == 0x7f) { // ^?
      kp->mod |= Ctrl;
      kp->c = '?';
    } else { // normal char (or part of char)
      kp->c = b;
    }

    ++nkps;
  }

  *out_nkeys = nkps;
}

struct keyboard_update keyboard_begin_frame(struct keyboard *kbd) {
  uint8_t bytes[32] = {0};
  int nbytes = read(STDIN_FILENO, bytes, 32);

  struct keyboard_update upd =
      (struct keyboard_update){.keys = {0}, .nkeys = 0};

  if (nbytes > 0) {
    parse_keys(bytes, nbytes, upd.keys, &upd.nkeys);
  }

  return upd;
}

void keyboard_end_frame(struct keyboard *kbd) {}

bool key_equal(struct key *key, uint8_t mod, uint8_t c) {
  return key->c == c && key->mod == mod;
}

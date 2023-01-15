#define _DEFAULT_SOURCE
#include "keyboard.h"
#include "reactor.h"
#include "stdio.h"
#include "utf8.h"

#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

struct keyboard keyboard_create(struct reactor *reactor) {
  struct termios term;
  tcgetattr(0, &term);

  // set input to non-blocking
  term.c_cc[VMIN] = 0;
  term.c_cc[VTIME] = 0;
  tcsetattr(0, TCSADRAIN, &term);

  return (struct keyboard){
      .reactor_event_id =
          reactor_register_interest(reactor, STDIN_FILENO, ReadInterest),
      .has_data = false,
  };
}

void parse_keys(uint8_t *bytes, uint32_t nbytes, struct key *out_keys,
                uint32_t *out_nkeys) {
  uint32_t nkps = 0;
  for (uint32_t bytei = 0; bytei < nbytes; ++bytei) {
    uint8_t b = bytes[bytei];

    if (b == 0x1b) { // meta
      struct key *kp = &out_keys[nkps];
      kp->start = bytei;
      kp->mod = Meta;
    } else if (b == '[' ||
               b == '0') { // special char (function keys, pgdn, etc)
      struct key *kp = &out_keys[nkps];
      if (kp->mod & Meta) {
        kp->mod = Spec;
      }
    } else if (b >= 0x00 && b <= 0x1f) { // ctrl char
      struct key *kp = &out_keys[nkps];
      kp->mod |= Ctrl;
      kp->key = b | 0x40;
      kp->start = bytei;
      kp->end = bytei + 1;
      ++nkps;
    } else if (b == 0x7f) { // ?
      struct key *kp = &out_keys[nkps];
      kp->mod |= Ctrl;
      kp->key = '?';
      kp->start = bytei;
      kp->end = bytei + 1;
      ++nkps;
    } else {
      struct key *kp = &out_keys[nkps];
      if (kp->mod & Spec && b == '~') {
        // skip tilde in special chars
        kp->end = bytei + 1;
        ++nkps;
      } else if (kp->mod & Meta || kp->mod & Spec) {
        kp->key = b;
        kp->end = bytei + 1;

        bool has_more = bytei + 1 < nbytes;

        if (kp->mod & Meta ||
            (kp->mod & Spec && !(has_more && bytes[bytei + 1] == '~'))) {
          ++nkps;
        }
      }
    }
  }

  *out_nkeys = nkps;
}

struct keyboard_update keyboard_update(struct keyboard *kbd,
                                       struct reactor *reactor) {

  struct keyboard_update upd = (struct keyboard_update){
      .keys = {0},
      .nkeys = 0,
      .nbytes = 0,
      .raw = {0},
  };

  if (!kbd->has_data) {
    if (reactor_poll_event(reactor, kbd->reactor_event_id)) {
      kbd->has_data = true;
    } else {
      return upd;
    }
  }

  int nbytes = read(STDIN_FILENO, upd.raw, 32);

  if (nbytes > 0) {
    upd.nbytes = nbytes;
    parse_keys(upd.raw, upd.nbytes, upd.keys, &upd.nkeys);

    if (nbytes < 32) {
      kbd->has_data = false;
    }
  } else if (nbytes == EAGAIN) {
    kbd->has_data = false;
  }

  return upd;
}

bool key_equal_char(struct key *key, uint8_t mod, uint8_t c) {
  return key->key == c && key->mod == mod;
}

bool key_equal(struct key *key1, struct key *key2) {
  return key1->key == key2->key && key1->mod == key2->mod;
}

void key_name(struct key *key, char *buf, size_t capacity) {
  const char *mod = "";
  switch (key->mod) {
  case Ctrl:
    mod = "c-";
    break;
  case Meta:
    mod = "m-";
    break;
  case Spec:
    mod = "special-";
    break;
  }

  snprintf(buf, capacity, "%s%c", mod, tolower(key->key));
}

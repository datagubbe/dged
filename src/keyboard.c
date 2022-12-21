#include "keyboard.h"
#include "reactor.h"
#include "stdio.h"
#include "utf8.h"

#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

struct keyboard keyboard_create(struct reactor *reactor) {
  // TODO: should input term stuff be set here?
  return (struct keyboard){
      .reactor_event_id =
          reactor_register_interest(reactor, STDIN_FILENO, ReadInterest),
      .has_data = false,
      .last_key = {0},
  };
}

void parse_keys(uint8_t *bytes, uint32_t nbytes, struct key *out_keys,
                uint32_t *out_nkeys, struct key *previous_key) {
  uint32_t nkps = 0;
  struct key *prevkp = previous_key;
  for (uint32_t bytei = 0; bytei < nbytes; ++bytei) {
    uint8_t b = bytes[bytei];

    struct key *kp = &out_keys[nkps];
    kp->start = bytei;

    bool inserted = true;
    if (b == 0x1b) { // meta
      kp->mod |= Meta;
    } else if (b >= 0x00 && b <= 0x1f) { // ctrl char
      kp->mod |= Ctrl;
      kp->key = b | 0x40;
    } else if (b == 0x7f) { // ^?
      kp->mod |= Ctrl;
      kp->key = '?';
    } else if (prevkp->mod & Meta) {
      prevkp->key = b;
      prevkp->end = bytei + 1;
      inserted = false;
    } else {
      inserted = false;
    }

    kp->end = bytei + 1;

    if (inserted) {
      ++nkps;
      prevkp = kp;
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
    parse_keys(upd.raw, upd.nbytes, upd.keys, &upd.nkeys, &kbd->last_key);

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
  }

  snprintf(buf, capacity, "%s%c", mod, tolower(key->key));
}

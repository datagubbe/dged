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
    kp->nbytes = 1;

    if (b == 0x1b) { // meta
      kp->mod |= Meta;
    } else if (b >= 0x00 && b <= 0x1f) { // ctrl char
      kp->mod |= Ctrl;
      kp->bytes[0] = b | 0x40;
      ++nkps;
      prevkp = kp;
    } else if (b == 0x7f) { // ^?
      kp->mod |= Ctrl;
      kp->bytes[0] = '?';
      ++nkps;
      prevkp = kp;
    } else if (utf8_byte_is_unicode_start((uint8_t)b)) {
      kp->bytes[0] = b;
      ++nkps;
      prevkp = kp;
    } else if (utf8_byte_is_unicode_continuation((uint8_t)b)) {
      prevkp->bytes[prevkp->nbytes] = b;
      ++prevkp->nbytes;
    } else { /* ascii char */
      if (prevkp->mod & Meta) {
        prevkp->bytes[0] = b;
      } else {
        kp->bytes[0] = b;
      }
      ++nkps;
      prevkp = kp;
    }
  }

  *out_nkeys = nkps;
}

struct keyboard_update keyboard_update(struct keyboard *kbd,
                                       struct reactor *reactor) {

  struct keyboard_update upd =
      (struct keyboard_update){.keys = {0}, .nkeys = 0};

  if (!kbd->has_data) {
    if (reactor_poll_event(reactor, kbd->reactor_event_id)) {
      kbd->has_data = true;
    } else {
      return upd;
    }
  }

  uint8_t bytes[32] = {0};
  int nbytes = read(STDIN_FILENO, bytes, 32);

  if (nbytes > 0) {
    parse_keys(bytes, nbytes, upd.keys, &upd.nkeys, &kbd->last_key);
  } else if (nbytes == EAGAIN) {
    kbd->has_data = false;
  }

  return upd;
}

bool key_equal_char(struct key *key, uint8_t mod, uint8_t c) {
  return key->bytes[0] == c && key->mod == mod;
}

bool key_equal(struct key *key1, struct key *key2) {
  return memcmp(key1->bytes, key2->bytes, key1->nbytes) == 0 &&
         key1->mod == key2->mod && key1->nbytes == key2->nbytes;
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

  uint8_t lower[6];
  for (uint32_t bytei = 0; bytei < key->nbytes; ++bytei) {
    lower[bytei] = tolower(key->bytes[bytei]);
  }

  snprintf(buf, capacity, "%s%.*s", mod, key->nbytes, lower);
}

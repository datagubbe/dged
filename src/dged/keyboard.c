#define _DEFAULT_SOURCE
#include "keyboard.h"
#include "reactor.h"
#include "stdio.h"
#include "utf8.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
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
  return keyboard_create_fd(reactor, STDIN_FILENO);
}

struct keyboard keyboard_create_fd(struct reactor *reactor, int fd) {
  return (struct keyboard){
      .fd = fd,
      .reactor_event_id = reactor_register_interest(reactor, fd, ReadInterest),
  };
}

void parse_keys(uint8_t *bytes, uint32_t nbytes, struct key *out_keys,
                uint32_t *out_nkeys) {
  uint32_t nkps = 0;
  for (uint32_t bytei = 0; bytei < nbytes; ++bytei) {
    uint8_t b = bytes[bytei];
    bool has_more = bytei + 1 < nbytes;
    uint8_t next = has_more ? bytes[bytei + 1] : 0;

    struct key *prev_kp = nkps > 0 ? &out_keys[nkps - 1] : NULL;
    struct key *kp = &out_keys[nkps];
    if (b == 0x1b) { // meta
      // meta key is special since it records as its own
      // key press so only set some state of the key press
      // and do not advance to the next since it is
      // not done yet
      kp->start = bytei;
      kp->mod = Meta;
    } else if (has_more && isalnum(next) && kp->mod & Meta &&
               (b == '[' ||
                b == '0')) { // special char (function keys, pgdn, etc)
      kp->mod = Spec;
    } else if (b == 0x7f) { // ?
      // For some reason, delete is not a control char
      kp->mod |= Ctrl;
      kp->key = '?';
      kp->start = bytei;
      kp->end = bytei + 1;
      ++nkps;
    } else if (iscntrl(b)) { // ctrl char
      kp->mod |= Ctrl;
      kp->key = b | 0x40;
      kp->start = bytei;
      kp->end = bytei + 1;
      ++nkps;
    } else {
      if ((kp->mod & Spec) && b == '~') {
        // skip tilde in special chars
        kp->end = bytei + 1;
        ++nkps;
      } else if ((kp->mod & Meta) || (kp->mod & Spec)) {
        // handle the key press following meta or spec.
        kp->key = b;
        kp->end = bytei + 1;
        if (kp->mod & Meta || (kp->mod & Spec && next != '~')) {
          ++nkps;
        }
      } else if (utf8_byte_is_unicode_continuation(b)) {
        // do nothing for these
      } else { // ascii char or unicode start byte (self-inserting)
        // TODO: do this better
        struct utf8_codepoint_iterator iter =
            create_utf8_codepoint_iterator(bytes + bytei, nbytes - bytei, 0);
        struct codepoint *codepoint = utf8_next_codepoint(&iter);
        if (codepoint != NULL) {
          uint32_t nb = codepoint->nbytes;

          // "compress" number of keys if previous key was also a
          // "simple" key
          if (prev_kp != NULL && prev_kp->mod == None) {
            prev_kp->end += nb;
          } else {
            kp->mod = None;
            kp->key = b;
            kp->start = bytei;
            kp->end = bytei + nb;
            ++nkps;
          }
        }
      }
    }
  }

  *out_nkeys = nkps;
}

struct keyboard_update keyboard_update(struct keyboard *kbd,
                                       struct reactor *reactor,
                                       void *(*frame_alloc)(size_t)) {

  struct keyboard_update upd = (struct keyboard_update){
      .keys = NULL,
      .nkeys = 0,
      .nbytes = 0,
      .raw = NULL,
  };

  // check if there is anything to do
  if (!reactor_poll_event(reactor, kbd->reactor_event_id)) {
    return upd;
  }

  // read all input in chunks of `bufsize` bytes
  const uint32_t bufsize = 1024;
  uint8_t *buf = malloc(bufsize), *writepos = buf;
  int nbytes = 0, nread = 0;
  while ((nread = read(kbd->fd, writepos, bufsize)) == bufsize) {
    nbytes += bufsize;
    buf = realloc(buf, nbytes + bufsize);
    writepos = buf + nbytes;
  }

  nbytes += nread;

  if (nbytes > 0) {
    upd.raw = frame_alloc(nbytes);

    if (upd.raw == NULL) {
      fprintf(stderr, "failed to allocate buffer of %d bytes\n", nbytes);
      free(buf);
      return upd;
    }

    upd.nbytes = nbytes;
    memcpy(upd.raw, buf, nbytes);
    upd.keys = frame_alloc(sizeof(struct key) * nbytes);
    memset(upd.keys, 0, sizeof(struct key) * nbytes);

    parse_keys(upd.raw, upd.nbytes, upd.keys, &upd.nkeys);
  }

  free(buf);
  return upd;
}

bool key_equal_char(struct key *key, uint8_t mod, uint8_t c) {
  return key->key == c && key->mod == mod;
}

bool key_equal(struct key *key1, struct key *key2) {
  return key_equal_char(key1, key2->mod, key2->key);
}

uint32_t key_name(struct key *key, char *buf, size_t capacity) {
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

  size_t written = snprintf(buf, capacity, "%s%c", mod, tolower(key->key));

  return written > capacity ? capacity : written;
}

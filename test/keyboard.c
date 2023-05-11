#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dged/keyboard.h"

#include "assert.h"
#include "fake-reactor.h"
#include "test.h"

struct call_count {
  uint32_t poll;
  uint32_t reg;
  uint32_t unreg;
};

bool fake_poll(void *userdata, uint32_t ev_id) {
  if (userdata != NULL) {
    struct call_count *cc = (struct call_count *)userdata;
    ++cc->poll;
  }
  return true;
}
uint32_t fake_register_interest(void *userdata, int fd,
                                enum interest interest) {
  if (userdata != NULL) {
    struct call_count *cc = (struct call_count *)userdata;
    ++cc->reg;
  }
  return 0;
}

void fake_unregister_interest(void *userdata, uint32_t ev_id) {
  if (userdata != NULL) {
    struct call_count *cc = (struct call_count *)userdata;
    ++cc->unreg;
  }
}

struct fake_keyboard {
  struct keyboard inner;
  struct reactor *reactor;
  int writefd;
};

struct fake_keyboard create_fake_keyboard(struct fake_reactor_impl *reactor) {
  struct reactor *r = fake_reactor_create(reactor);

  int pipefd[2];
  int res = pipe(pipefd);
  ASSERT(res == 0, "Failed to create a pipe?");

  struct keyboard k = keyboard_create_fd(r, pipefd[0]);

  return (struct fake_keyboard){
      .inner = k,
      .reactor = r,
      .writefd = pipefd[1],
  };
}

void fake_keyboard_write(struct fake_keyboard *kbd, const char *s) {
  if (write(kbd->writefd, s, strlen(s)) < 0) {
    printf("write to kbd fd failed: %s\n", strerror(errno));
  }
}

void fake_keyboard_close_write(struct fake_keyboard *kbd) {
  close(kbd->writefd);
}

void fake_keyboard_destroy(struct fake_keyboard *kbd) {
  fake_keyboard_close_write(kbd);
  reactor_destroy(kbd->reactor);
}

void simple_key() {
  struct call_count cc = {0};
  struct fake_reactor_impl fake = {
      .poll_event = fake_poll,
      .register_interest = fake_register_interest,
      .unregister_interest = fake_unregister_interest,
      .userdata = &cc,
  };
  struct fake_keyboard k = create_fake_keyboard(&fake);
  ASSERT(cc.reg == 1, "Expected keyboard to register read interest");

  fake_keyboard_write(&k, "q");
  fake_keyboard_close_write(&k);

  struct keyboard_update upd = keyboard_update(&k.inner, k.reactor, malloc);

  ASSERT(upd.nkeys == 1, "Expected to get 1 key from update");
  ASSERT(cc.poll, "Expected keyboard update to call reactor poll");

  fake_keyboard_destroy(&k);
  free(upd.keys);
  free(upd.raw);
}

void ctrl_key() {
  struct fake_reactor_impl fake = {
      .poll_event = fake_poll,
      .register_interest = fake_register_interest,
      .unregister_interest = fake_unregister_interest,
      .userdata = NULL,
  };
  struct fake_keyboard k = create_fake_keyboard(&fake);
  fake_keyboard_write(&k, "");
  fake_keyboard_close_write(&k);

  struct keyboard_update upd = keyboard_update(&k.inner, k.reactor, malloc);
  ASSERT(upd.nkeys == 2, "Expected to get 2 keys from update");
  ASSERT(upd.keys[0].mod == Ctrl && upd.keys[0].key == 'H',
         "Expected first key to be c-h");
  ASSERT(upd.keys[1].mod == Ctrl && upd.keys[1].key == 'P',
         "Expected first key to be c-p");

  fake_keyboard_destroy(&k);
  free(upd.keys);
  free(upd.raw);
}

void meta_key() {
  struct fake_reactor_impl fake = {
      .poll_event = fake_poll,
      .register_interest = fake_register_interest,
      .unregister_interest = fake_unregister_interest,
      .userdata = NULL,
  };
  struct fake_keyboard k = create_fake_keyboard(&fake);
  fake_keyboard_write(&k, "d[x");
  fake_keyboard_close_write(&k);

  struct keyboard_update upd = keyboard_update(&k.inner, k.reactor, malloc);
  ASSERT(upd.nkeys == 3, "Expected to get 3 keys from update");
  ASSERT(upd.keys[0].mod == Meta && upd.keys[0].key == 'd',
         "Expected first key to be m-d");
  ASSERT(upd.keys[1].mod == Meta && upd.keys[1].key == '[',
         "Expected second key to be m-[");
  ASSERT(upd.keys[2].mod == Meta && upd.keys[2].key == 'x',
         "Expected third key to be m-x");

  fake_keyboard_destroy(&k);
  free(upd.keys);
  free(upd.raw);
}

void spec_key() {
  struct fake_reactor_impl fake = {
      .poll_event = fake_poll,
      .register_interest = fake_register_interest,
      .unregister_interest = fake_unregister_interest,
      .userdata = NULL,
  };
  struct fake_keyboard k = create_fake_keyboard(&fake);
  fake_keyboard_write(&k, "[A[6~");
  fake_keyboard_close_write(&k);

  struct keyboard_update upd = keyboard_update(&k.inner, k.reactor, malloc);
  ASSERT(upd.nkeys == 2, "Expected to get 2 keys from update");
  ASSERT(upd.keys[0].mod == Spec && upd.keys[0].key == 'A',
         "Expected first key to be up arrow");
  ASSERT(upd.keys[1].mod == Spec && upd.keys[1].key == '6',
         "Expected second key to be PgDn");

  fake_keyboard_destroy(&k);
  free(upd.keys);
  free(upd.raw);
}

void test_utf8() {
  struct fake_reactor_impl fake = {
      .poll_event = fake_poll,
      .register_interest = fake_register_interest,
      .unregister_interest = fake_unregister_interest,
      .userdata = NULL,
  };
  struct fake_keyboard k = create_fake_keyboard(&fake);
  fake_keyboard_write(&k, "🎠");
  fake_keyboard_close_write(&k);

  struct keyboard_update upd = keyboard_update(&k.inner, k.reactor, malloc);
  ASSERT(upd.nbytes == 4, "Expected there to be four bytes of raw input");
  ASSERT(upd.nkeys == 1, "Expected to get 1 key from update");
  ASSERT(upd.keys[0].start == 0 && upd.keys[0].end == 4,
         "Expected first key to be 4 bytes");

  fake_keyboard_destroy(&k);
  free(upd.keys);
  free(upd.raw);
}

void test_key_equal() {
  struct key k1 = {.mod = Ctrl, .key = 'A'};
  ASSERT(key_equal(&k1, &k1), "Expected key to be equal to itself");
  ASSERT(key_equal_char(&k1, Ctrl, 'A'), "Expected key to be c-a");

  struct key k2 = {.mod = None, .key = 'A'};
  ASSERT(!key_equal(&k1, &k2), "Expected key to not be equal to different key");
  ASSERT(!key_equal_char(&k2, Spec, 'A'),
         "Expected yet another different key to not be the same");
}

void run_keyboard_tests() {
  run_test(simple_key);
  run_test(ctrl_key);
  run_test(meta_key);
  run_test(spec_key);
  run_test(test_utf8);
  run_test(test_key_equal);
}

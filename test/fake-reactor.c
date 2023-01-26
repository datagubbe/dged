#include "fake-reactor.h"
#include <stdlib.h>

struct reactor {
  struct fake_reactor_impl *impl;
};

struct reactor *reactor_create() {
  return (struct reactor *)calloc(1, sizeof(struct reactor));
}

void reactor_destroy(struct reactor *reactor) { free(reactor); }

void reactor_update(struct reactor *reactor) {}
bool reactor_poll_event(struct reactor *reactor, uint32_t ev_id) {
  if (reactor->impl != NULL) {
    return reactor->impl->poll_event(reactor->impl->userdata, ev_id);
  } else {
    return false;
  }
}

uint32_t reactor_register_interest(struct reactor *reactor, int fd,
                                   enum interest interest) {
  if (reactor->impl != NULL) {
    return reactor->impl->register_interest(reactor->impl->userdata, fd,
                                            interest);
  } else {
    return 0;
  }
}

void reactor_unregister_interest(struct reactor *reactor, uint32_t ev_id) {
  if (reactor->impl != NULL) {
    return reactor->impl->unregister_interest(reactor->impl->userdata, ev_id);
  }
}

struct reactor *fake_reactor_create(struct fake_reactor_impl *impl) {
  struct reactor *r = reactor_create();
  set_reactor_impl(r, impl);
  return r;
}

void set_reactor_impl(struct reactor *reactor, struct fake_reactor_impl *impl) {
  reactor->impl = impl;
}

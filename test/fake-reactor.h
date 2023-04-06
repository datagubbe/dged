#include <stdbool.h>
#include <stdint.h>

#include "dged/reactor.h"

struct fake_reactor_impl {
  bool (*poll_event)(void *userdata, uint32_t ev_id);
  uint32_t (*register_interest)(void *userdata, int fd, enum interest interest);
  void (*unregister_interest)(void *userdata, uint32_t ev_id);
  void *userdata;
};

struct reactor *fake_reactor_create(struct fake_reactor_impl *impl);
void set_reactor_impl(struct reactor *reactor, struct fake_reactor_impl *impl);

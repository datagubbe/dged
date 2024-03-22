#include "reactor.h"

#include <stdlib.h>

struct reactor {

};

struct reactor *reactor_create() {
  struct reactor *reactor = calloc(1, sizeof(struct reactor));

  return reactor;
}

void reactor_destroy(struct reactor *reactor) {
  free(reactor);
}

void reactor_update(struct reactor *reactor) {

}

bool reactor_poll_event(struct reactor *reactor, uint32_t ev_id) {
  return false;
}

uint32_t reactor_register_interest(struct reactor *reactor, int fd, enum interest interest) {
  return -1;
}

uint32_t reactor_watch_file(struct reactor *reactor, const char *path, uint32_t mask) {
  return -1;
}

void reactor_unwatch_file(struct reactor *reactor, uint32_t id) {

}

bool reactor_next_file_event(struct reactor *reactor, struct file_event *out) {
  return false;
}

void reactor_unregister_interest(struct reactor *reactor, uint32_t ev_id) {

}

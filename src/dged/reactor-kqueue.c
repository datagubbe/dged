#include "reactor.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/types.h>
#include <unistd.h>

#include "minibuffer.h"

struct reactor {
  int queue;
  struct kevent events[16];
  uint32_t nevents;
};

struct reactor *reactor_create() {
  int queue = kqueue();
  if (queue < 0) {
    return NULL;
  }

  struct reactor *reactor = calloc(1, sizeof(struct reactor));
  reactor->queue = queue;
  reactor->nevents = 0;
  return reactor;
}

void reactor_destroy(struct reactor *reactor) {
  close(reactor->queue);
  reactor->queue = -1;
  free(reactor);
}

void reactor_update(struct reactor *reactor) {
  int events = kevent(reactor->queue, NULL, 0, reactor->events, 16, NULL);
  if (events == -1) {
    // TODO: what to do here?
    return;
  }

  reactor->nevents = events;
}

bool reactor_poll_event(struct reactor *reactor, uint32_t ev_id) {
  for (uint32_t ei = 0; ei < reactor->nevents; ++ei) {
    struct kevent *ev = &reactor->events[ei];

    if (ev->ident == ev_id) {
      return true;
    }
  }

  return false;
}

uint32_t reactor_register_interest(struct reactor *reactor, int fd,
                                   enum interest interest) {
  struct kevent changes[2] = {0};
  uint32_t nchanges = 0;

  if ((interest & ReadInterest) != 0) {
    EV_SET(&changes[0], fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
    ++nchanges;
  }

  if ((interest & WriteInterest) != 0) {
    EV_SET(&changes[1], fd, EVFILT_WRITE, EV_ADD, 0, 0, NULL);
    ++nchanges;
  }

  if (kevent(reactor->queue, changes, nchanges, NULL, 0, NULL) < 0) {
    return -1;
  }

  return fd;
}

uint32_t reactor_watch_file(struct reactor *reactor, const char *path,
                            uint32_t mask) {

  uint32_t fflags = NOTE_WRITE | NOTE_DELETE | NOTE_RENAME | NOTE_REVOKE;
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    minibuffer_echo_timeout(4, "failed to watch %s: %s", path, strerror(errno));
    return 0;
  }

  struct kevent new_event;
  EV_SET(&new_event, fd, EVFILT_VNODE, EV_ADD | EV_CLEAR | EV_ENABLE, fflags, 0,
         NULL);
  if (kevent(reactor->queue, &new_event, 1, NULL, 0, NULL) < 0) {
    return 0;
  }

  return fd;
}

void reactor_unwatch_file(struct reactor *reactor, uint32_t id) {
  // all kevents for this fd are removed automatically when closed
  close(id);
}

bool reactor_next_file_event(struct reactor *reactor, struct file_event *out) {

  // find the next vnode event and pop it from the events
  struct kevent ev;
  bool found = false;
  for (uint32_t e = 0; e < reactor->nevents; ++e) {
    if (reactor->events[e].filter == EVFILT_VNODE) {
      ev = reactor->events[e];
      reactor->events[e] = reactor->events[reactor->nevents - 1];
      --reactor->nevents;
      found = true;
      break;
    }
  }

  if (!found) {
    return false;
  }

  out->mask = FileWritten;
  if ((ev.fflags & NOTE_DELETE) || (ev.fflags & NOTE_RENAME) ||
      (ev.fflags & NOTE_REVOKE)) {
    out->mask |= LastEvent;
  }

  out->id = ev.ident;
  return true;
}

void reactor_unregister_interest(struct reactor *reactor, uint32_t ev_id) {
  struct kevent changes[2] = {0};
  EV_SET(&changes[0], ev_id, EVFILT_READ, EV_DELETE, 0, 0, NULL);
  EV_SET(&changes[1], ev_id, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);

  kevent(reactor->queue, changes, 2, NULL, 0, NULL);
}

#include "minibuffer.h"
#include "reactor.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <unistd.h>

struct reactor {
  int epoll_fd;
  struct events *events;
  int inotify_fd;
  uint32_t inotify_poll_id;
};

struct events {
  struct epoll_event events[10];
  uint32_t nevents;
};

struct reactor *reactor_create() {
  int epollfd = epoll_create1(0);
  if (epollfd == -1) {
    perror("epoll_create1");
  }

  int inotifyfd = inotify_init1(IN_NONBLOCK);
  if (inotifyfd == -1) {
    perror("inotify_init1");
  }

  struct reactor *r = (struct reactor *)calloc(1, sizeof(struct reactor));
  r->epoll_fd = epollfd;
  r->events = calloc(1, sizeof(struct events));
  r->inotify_fd = inotifyfd;
  r->inotify_poll_id = reactor_register_interest(r, inotifyfd, ReadInterest);

  return r;
}

void reactor_destroy(struct reactor *reactor) {
  free(reactor->events);
  free(reactor);
}

uint32_t reactor_register_interest(struct reactor *reactor, int fd,
                                   enum interest interest) {
  struct epoll_event ev;
  ev.events = 0;
  ev.events |= (interest & ReadInterest) != 0 ? EPOLLIN : 0;
  ev.events |= (interest & WriteInterest) != 0 ? EPOLLOUT : 0;
  ev.data.fd = fd;
  if (epoll_ctl(reactor->epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
    perror("epoll_ctl");
    return -1;
  }

  return fd;
}

void reactor_unregister_interest(struct reactor *reactor, uint32_t ev_id) {
  epoll_ctl(reactor->epoll_fd, EPOLL_CTL_DEL, ev_id, NULL);
}

bool reactor_poll_event(struct reactor *reactor, uint32_t ev_id) {
  struct events *events = (struct events *)reactor->events;
  for (uint32_t ei = 0; ei < events->nevents; ++ei) {
    struct epoll_event *ev = &events->events[ei];

    if (ev->data.fd == ev_id) {
      return true;
    }
  }

  return false;
}

uint32_t reactor_watch_file(struct reactor *reactor, const char *path,
                            uint32_t mask) {
  // TODO: change if we get more event types
  mask = IN_MODIFY;

  int fd = inotify_add_watch(reactor->inotify_fd, path, mask);
  if (fd == -1) {
    minibuffer_echo_timeout(4, "failed to watch %s: %s", path, strerror(errno));
    return 0;
  }

  return (uint32_t)fd;
}

void reactor_unwatch_file(struct reactor *reactor, uint32_t id) {
  inotify_rm_watch(reactor->inotify_fd, id);
}

bool reactor_next_file_event(struct reactor *reactor, struct file_event *out) {
  if (reactor_poll_event(reactor, reactor->inotify_poll_id)) {
    ssize_t sz = sizeof(struct inotify_event) + NAME_MAX + 1;
    uint8_t buf[sz];
    ssize_t bytes_read = read(reactor->inotify_fd, buf, sz);
    if (bytes_read < 0) {
      return false;
    }

    struct inotify_event *ev = (struct inotify_event *)buf;
    // TODO: change when adding more of these
    out->mask = FileWritten;
    if (ev->mask & IN_IGNORED != 0) {
      out->mask |= LastEvent;
    }
    out->id = (uint32_t)ev->wd;
    return true;
  }

  return false;
}

void reactor_update(struct reactor *reactor) {
  struct events *events = reactor->events;
  int nfds = epoll_wait(reactor->epoll_fd, events->events, 10, -1);

  if (nfds == -1) {
    // TODO: log failure
  }

  events->nevents = nfds;
}

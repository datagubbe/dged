#include "reactor.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>

struct events {
  struct epoll_event events[10];
  uint32_t nevents;
};

struct reactor reactor_create() {
  int epollfd = epoll_create1(0);
  if (epollfd == -1) {
    perror("epoll_create1");
  }

  return (struct reactor){
      .epoll_fd = epollfd,
      .events = calloc(1, sizeof(struct events)),
  };
}

void reactor_destroy(struct reactor *reactor) { free(reactor->events); }

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

void reactor_update(struct reactor *reactor) {
  struct events *events = (struct events *)reactor->events;
  int nfds = epoll_wait(reactor->epoll_fd, events->events, 10, -1);

  if (nfds == -1) {
    // TODO: log failure
  }

  events->nevents = nfds;
}

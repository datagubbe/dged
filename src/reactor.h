#include <stdbool.h>
#include <stdint.h>

enum interest {
  ReadInterest = 1,
  WriteInterest = 2,
};

struct reactor {
  int epoll_fd;
  void *events;
};

struct reactor reactor_create();
void reactor_destroy(struct reactor *reactor);
void reactor_update(struct reactor *reactor);
bool reactor_poll_event(struct reactor *reactor, uint32_t ev_id);
uint32_t reactor_register_interest(struct reactor *reactor, int fd,
                                   enum interest interest);
void reactor_unregister_interest(struct reactor *reactor, uint32_t ev_id);

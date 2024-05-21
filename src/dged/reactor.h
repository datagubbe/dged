#include <stdbool.h>
#include <stdint.h>

enum interest {
  ReadInterest = 1,
  WriteInterest = 2,
};

enum file_event_type {
  FileWritten = 1 << 0,
  LastEvent = 1 << 1,
};

struct file_event {
  uint32_t mask;
  uint32_t id;
};

struct reactor;

struct reactor *reactor_create(void);
void reactor_destroy(struct reactor *reactor);
void reactor_update(struct reactor *reactor);
bool reactor_poll_event(struct reactor *reactor, uint32_t ev_id);
uint32_t reactor_register_interest(struct reactor *reactor, int fd,
                                   enum interest interest);
uint32_t reactor_watch_file(struct reactor *reactor, const char *path,
                            uint32_t mask);
void reactor_unwatch_file(struct reactor *reactor, uint32_t id);
bool reactor_next_file_event(struct reactor *reactor, struct file_event *out);
void reactor_unregister_interest(struct reactor *reactor, uint32_t ev_id);

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

struct minibuffer_update {
  struct render_cmd *cmds;
  uint64_t ncmds;
};

struct minibuffer {
  uint8_t *buffer;
  uint32_t capacity;
  uint32_t nbytes;
  uint32_t row;
  bool dirty;
  struct timespec expires;
};

typedef void *(alloc_fn)(size_t);

void minibuffer_init(uint32_t row);
void minibuffer_destroy();

struct minibuffer_update minibuffer_update(alloc_fn frame_alloc);

void minibuffer_echo(const char *fmt, ...);
void minibuffer_echo_timeout(uint32_t timeout, const char *fmt, ...);
void minibuffer_clear();
bool minibuffer_displaying();

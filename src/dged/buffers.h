#include <stdint.h>

struct buffer;

struct buffers {
  // TODO: more buffers
  struct buffer *buffers;
  uint32_t nbuffers;
  uint32_t capacity;
};

void buffers_init(struct buffers *buffers, uint32_t initial_capacity);

struct buffer *buffers_add(struct buffers *buffers, struct buffer buffer);
struct buffer *buffers_find(struct buffers *buffers, const char *name);

void buffers_destroy(struct buffers *buffers);

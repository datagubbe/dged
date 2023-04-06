#include "buffers.h"
#include "buffer.h"

#include <stdlib.h>
#include <string.h>

void buffers_init(struct buffers *buffers, uint32_t initial_capacity) {
  buffers->buffers = calloc(initial_capacity, sizeof(struct buffer));
  buffers->nbuffers = 0;
  buffers->capacity = initial_capacity;
}

struct buffer *buffers_add(struct buffers *buffers, struct buffer buffer) {
  if (buffers->nbuffers == buffers->capacity) {
    buffers->capacity *= 2;
    buffers->buffers =
        realloc(buffers->buffers, sizeof(struct buffer) * buffers->capacity);
  }

  buffers->buffers[buffers->nbuffers] = buffer;
  ++buffers->nbuffers;

  return &buffers->buffers[buffers->nbuffers - 1];
}

struct buffer *buffers_find(struct buffers *buffers, const char *name) {
  for (uint32_t bufi = 0; bufi < buffers->nbuffers; ++bufi) {
    if (strcmp(name, buffers->buffers[bufi].name) == 0) {
      return &buffers->buffers[bufi];
    }
  }

  return NULL;
}

void buffers_destroy(struct buffers *buffers) {
  for (uint32_t bufi = 0; bufi < buffers->nbuffers; ++bufi) {
    buffer_destroy(&buffers->buffers[bufi]);
  }

  buffers->nbuffers = 0;
  free(buffers->buffers);
}

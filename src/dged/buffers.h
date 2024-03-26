#include "vec.h"

#include <stdbool.h>
#include <stdint.h>

struct buffer;

typedef void (*buffers_hook_cb)(struct buffer *buffer, void *userdata);

struct buffers_hook {
  buffers_hook_cb callback;
  void *userdata;
};

struct buffer_entry;

struct buffers {
  VEC(struct buffer_entry) buffers;
  VEC(struct buffers_hook) add_hooks;
  VEC(struct buffers_hook) remove_hooks;
};

void buffers_init(struct buffers *buffers, uint32_t initial_capacity);

struct buffer *buffers_add(struct buffers *buffers, struct buffer buffer);
struct buffer *buffers_find(struct buffers *buffers, const char *name);
struct buffer *buffers_find_by_filename(struct buffers *buffers,
                                        const char *path);

bool buffers_remove(struct buffers *buffers, const char *name);

uint32_t buffers_add_add_hook(struct buffers *buffers, buffers_hook_cb callback,
                              void *userdata);
uint32_t buffers_add_remove_hook(struct buffers *buffers,
                                 buffers_hook_cb callback, void *userdata);

void buffers_for_each(struct buffers *buffers, buffers_hook_cb callback,
                      void *userdata);

uint32_t buffers_num_buffers(struct buffers *buffers);
struct buffer *buffers_first(struct buffers *buffers);

void buffers_destroy(struct buffers *buffers);

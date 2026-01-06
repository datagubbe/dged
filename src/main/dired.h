#ifndef _DIRED_H
#define _DIRED_H

#include <stdbool.h>

struct buffer;
struct commands;
struct frame_allocator;

bool dired_is_dired_buffer(struct buffer *);

void register_dired_commands(struct commands *, struct frame_allocator *);

#endif

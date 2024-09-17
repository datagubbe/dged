#ifndef _FRAME_HOOKS_H
#define _FRAME_HOOKS_H

#include <stddef.h>

typedef void (*next_frame_cb)(void *);

void init_frame_hooks(void);
void teardown_frame_hooks(void);
void run_next_frame(next_frame_cb callback, void *userdata);
size_t dispatch_next_frame_hooks(void);

#endif

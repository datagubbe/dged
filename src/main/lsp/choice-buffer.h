#ifndef _CHOICE_BUFFER_H
#define _CHOICE_BUFFER_H

#include "dged/s8.h"

typedef void (*abort_callback)(void *);
typedef void (*select_callback)(void *, void *);
typedef void (*update_callback)(void *);

struct choice_buffer;
struct buffers;

struct choice_buffer *
choice_buffer_create(struct s8 title, struct buffers *buffers,
                     select_callback selected, abort_callback aborted,
                     update_callback update, void *userdata);
void choice_buffer_add_choice(struct choice_buffer *buffer, struct s8 text,
                              void *data);
void choice_buffer_add_choice_with_callback(struct choice_buffer *buffer,
                                            struct s8 text, void *data,
                                            select_callback callback);

#endif

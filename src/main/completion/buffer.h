#ifndef _MAIN_COMPLETION_BUFFER_H
#define _MAIN_COMPLETION_BUFFER_H

struct buffer;
struct buffers;

/**
 * Create a new buffer completion provider.
 *
 * This provider completes buffer names from the
 * buffer list.
 * @returns A buffer name @ref completion_provider.
 */
struct completion_provider
create_buffer_provider(struct buffers *buffers,
                       void (*on_buffer_selected)(struct buffer *));

#endif

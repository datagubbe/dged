#ifndef _LSP_COMPLETION_H
#define _LSP_COMPLETION_H

#include "dged/vec.h"

struct completion_ctx;
struct buffer;
struct lsp_server;

typedef VEC(struct s8) triggerchar_vec;

struct completion_ctx *create_completion_ctx(struct lsp_server *server,
                                             triggerchar_vec *trigger_chars);
void destroy_completion_ctx(struct completion_ctx *);

void enable_completion_for_buffer(struct completion_ctx *, struct buffer *);

#endif

#ifndef _LSP_REFERENCES_H
#define _LSP_REFERENCES_H

#include <stdint.h>

#include "dged/command.h"
#include "dged/location.h"

struct lsp_server;
struct buffer;
struct buffers;

void lsp_references(struct lsp_server *server, struct buffer *buffer,
                    struct location at, struct buffers *buffers);

int32_t lsp_references_cmd(struct command_ctx ctx, int argc,
                           const char *argv[]);

#endif

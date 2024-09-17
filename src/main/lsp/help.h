#ifndef _LSP_HELP_H
#define _LSP_HELP_H

#include "dged/command.h"
#include "dged/location.h"

struct buffer;
struct buffers;
struct lsp_server;

void lsp_help(struct lsp_server *, struct buffer *, struct location,
              struct buffers *);

int32_t lsp_help_cmd(struct command_ctx, int, const char **);

#endif

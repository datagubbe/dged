#ifndef _LSP_RENAME_H
#define _LSP_RENAME_H

#include "dged/command.h"
#include "dged/location.h"
#include "dged/s8.h"

struct lsp_server;
struct buffer;

void lsp_rename(struct lsp_server *, struct buffer *, struct location,
                struct s8);

int32_t lsp_rename_cmd(struct command_ctx, int, const char **);

#endif

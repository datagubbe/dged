#ifndef _FORMAT_H
#define _FORMAT_H

#include "dged/command.h"
#include "dged/location.h"

struct buffer;
struct lsp_server;
struct lsp_response;

void format_document(struct lsp_server *, struct buffer *);
void format_document_save(struct lsp_server *, struct buffer *);
void format_region(struct lsp_server *, struct buffer *, struct region);

/* COMMANDS */
int32_t format_cmd(struct command_ctx, int, const char **);

#endif

#ifndef _GOTO_H
#define _GOTO_H

#include "dged/command.h"

#include "types.h"

struct lsp_server;
struct buffers;

void init_goto(size_t jump_stack_depth, struct buffers *);
void destroy_goto(void);

void lsp_jump_to(struct text_document_location loc);

/* COMMANDS */
int32_t lsp_goto_def_cmd(struct command_ctx, int, const char **);
int32_t lsp_goto_decl_cmd(struct command_ctx, int, const char **);
int32_t lsp_goto_impl_cmd(struct command_ctx, int, const char **);
int32_t lsp_goto_cmd(struct command_ctx, int, const char **);
int32_t lsp_goto_previous_cmd(struct command_ctx, int, const char **);

#endif

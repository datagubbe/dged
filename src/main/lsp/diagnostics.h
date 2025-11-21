#ifndef _DIAGNOSTICS_H
#define _DIAGNOSTICS_H

#include "dged/command.h"
#include "dged/text.h"
#include "main/lsp/types.h"

struct lsp_server;
struct buffers;
struct lsp_notification;

struct lsp_diagnostics;

struct lsp_buffer_diagnostics {
  struct buffer *buffer;
  layer_id layer;
  diagnostic_vec diagnostics;
};

struct lsp_diagnostics *diagnostics_create(void);
void diagnostics_destroy(struct lsp_diagnostics *);

struct lsp_buffer_diagnostics *diagnostics_for_buffer(struct lsp_diagnostics *,
                                                      struct buffer *);
void handle_publish_diagnostics(struct lsp_server *, struct buffers *,
                                struct lsp_notification *);

/* COMMANDS */
int32_t diagnostics_cmd(struct command_ctx, int, const char **);
int32_t next_diagnostic_cmd(struct command_ctx, int, const char **);
int32_t prev_diagnostic_cmd(struct command_ctx, int, const char **);

#endif

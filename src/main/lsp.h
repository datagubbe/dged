#ifndef _MAIN_LSP_H
#define _MAIN_LSP_H

#include <stddef.h>

#include "dged/location.h"
#include "dged/lsp.h"
#include "dged/s8.h"
#include "dged/vec.h"

#include "lsp/types.h"

struct reactor;
struct buffers;
struct commands;

void lang_servers_init(struct reactor *reactor, struct buffers *buffers,
                       struct commands *commands);
void lang_servers_update(void);
void lang_servers_teardown(void);

struct lsp_server;
struct buffer;
struct workspace_edit;

struct lsp_server *lsp_server_for_lang_id(const char *id);
struct lsp_server *lsp_server_for_buffer(struct buffer *buffer);

bool lsp_server_active(struct lsp_server *server);
void lsp_server_reload(struct lsp_server *server);
void lsp_server_shutdown(struct lsp_server *server);
struct lsp *lsp_backend(struct lsp_server *server);

bool apply_edits(struct lsp_server *server,
                 const struct workspace_edit *ws_edit);

void apply_edits_buffer(struct lsp_server *, struct buffer *, text_edit_vec,
                        struct location *);

typedef void (*response_handler)(struct lsp_server *, struct lsp_response *,
                                 void *);
uint64_t new_pending_request(struct lsp_server *server,
                             response_handler handler, void *userdata);

struct region lsp_range_to_coordinates(struct lsp_server *server,
                                       struct buffer *buffer,
                                       struct region range);

struct region region_to_lsp(struct buffer *buffer, struct region region,
                            struct lsp_server *server);

struct lsp_diagnostics *lsp_server_diagnostics(struct lsp_server *server);

#endif

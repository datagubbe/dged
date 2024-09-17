#ifndef _LSP_H
#define _LSP_H

#include <stddef.h>

#include "json.h"
#include "jsonrpc.h"
#include "location.h"
#include "s8.h"

struct buffer;
struct lsp;
struct reactor;

typedef uint64_t request_id;

struct lsp_response_error {
  int code;
  struct s8 message;
  struct json_value data;
};

enum lsp_message_type {
  Lsp_Notification,
  Lsp_Request,
  Lsp_Response,
};

struct lsp_response {
  request_id id;

  bool ok;
  union data {
    struct json_value result;
    struct lsp_response_error error;
  } value;
};

struct lsp_request {
  request_id id;
  struct s8 method;
  struct json_value params;
};

struct lsp_notification {
  struct s8 method;
  struct json_value params;
};

struct lsp_message {
  enum lsp_message_type type;
  bool parsed;
  union message_data {
    struct lsp_response response;
    struct lsp_request request;
    struct lsp_notification notification;
  } message;

  struct s8 payload;
  struct jsonrpc_message jsonrpc_msg;
};

// lifecycle functions
struct lsp *lsp_create(char *const command[], struct reactor *reactor,
                       struct buffer *stderr_buffer, const char *name);
uint32_t lsp_update(struct lsp *lsp, struct lsp_message *msgs,
                    uint32_t nmax_msgs);
void lsp_destroy(struct lsp *lsp);

// process control functions
int lsp_start_server(struct lsp *lsp);
int lsp_restart_server(struct lsp *lsp);
void lsp_stop_server(struct lsp *lsp);
bool lsp_server_running(const struct lsp *lsp);
uint64_t lsp_server_pid(const struct lsp *lsp);
const char *lsp_server_name(const struct lsp *lsp);

// lsp message creation
struct lsp_message lsp_create_request(request_id id, struct s8 method,
                                      struct s8 payload);
struct lsp_message lsp_create_notification(struct s8 method, struct s8 payload);
struct lsp_message lsp_create_response(request_id id, bool ok,
                                       struct s8 payload);

void lsp_message_destroy(struct lsp_message *message);

// protocol functions
void lsp_send(struct lsp *lsp, struct lsp_message message);

#endif

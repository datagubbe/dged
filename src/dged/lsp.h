#ifndef _LSP_H
#define _LSP_H

#include "json.h"
#include "location.h"
#include "s8.h"

struct buffer;
struct lsp;
struct reactor;

typedef uint64_t request_id;

struct lsp_response_error {};

struct lsp_response {
  request_id id;

  bool ok;
  union payload_data {
    struct json_value result;
    struct lsp_response_error error;
  } payload;
};

struct lsp_request {
  struct s8 method;
  struct json_object *params;
};

struct lsp_notification {
  int something;
};

struct lsp_client {
  void (*log_message)(int type, struct s8 msg);
};

// lifecycle functions
struct lsp *lsp_create(char *const command[], struct reactor *reactor,
                       struct buffer *stderr_buffer,
                       struct lsp_client client_impl, const char *name);
uint32_t lsp_update(struct lsp *lsp, struct lsp_response **responses,
                    uint32_t responses_capacity);
void lsp_destroy(struct lsp *lsp);

// process control functions
int lsp_start_server(struct lsp *lsp);
int lsp_restart_server(struct lsp *lsp);
void lsp_stop_server(struct lsp *lsp);
bool lsp_server_running(const struct lsp *lsp);
uint64_t lsp_server_pid(const struct lsp *lsp);
const char *lsp_server_name(const struct lsp *lsp);

// protocol functions
request_id lsp_request(struct lsp *lsp, struct lsp_request request);

#endif

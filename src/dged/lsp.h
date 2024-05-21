#ifndef _LSP_H
#define _LSP_H

#include "location.h"
#include "s8.h"

struct buffer;
struct lsp;
struct reactor;

typedef uint32_t request_id;

struct lsp_response {
  request_id id;
  bool ok;
  union payload_data {
    void *result;
    struct s8 error;
  } payload;
};

struct lsp_notification {
  int something;
};

struct lsp_client {
  void (*log_message)(int type, struct s8 msg);
};

struct hover {
  struct s8 contents;

  bool has_range;
  struct region *range;
};

struct text_doc_item {
  struct s8 uri;
  struct s8 language_id;
  uint32_t version;
  struct s8 text;
};

struct text_doc_position {
  struct s8 uri;
  struct location pos;
};

struct initialize_params {
  struct s8 client_name;
  struct s8 client_version;
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
void lsp_initialize(struct lsp *lsp, struct initialize_params);
void lsp_did_open_document(struct lsp *lsp, struct text_doc_item document);
request_id lsp_hover(struct lsp *lsp, struct text_doc_position);

#endif

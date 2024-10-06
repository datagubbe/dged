#ifndef _JSONRPC_H
#define _JSONRPC_H

#include "json.h"
#include "s8.h"

struct jsonrpc_request {
  struct json_value id;
  struct s8 method;
  struct json_object *params;
};

struct jsonrpc_response {
  struct json_value id;
  bool ok;
  union {
    struct json_value result;
    struct jsonrpc_error error;
  } value;
};

struct jsonrpc_error {
  int code;
  struct s8 message;
  struct json_value data;
};

struct jsonrpc_request jsonrpc_request_create(struct s8 method, struct json_object *params);
struct jsonrpc_response jsonrpc_parse_response(const uint8_t *buf, uint64_t size);
struct s8 jsonrpc_request_to_string(const struct jsonprc_request *request);

#endif

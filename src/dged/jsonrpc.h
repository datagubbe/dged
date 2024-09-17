#ifndef _JSONRPC_H
#define _JSONRPC_H

#include <stdint.h>

#include "json.h"
#include "s8.h"

enum jsonrpc_type {
  Jsonrpc_Request,
  Jsonrpc_Response,
  Jsonrpc_Notification,
};

struct jsonrpc_request {
  struct json_value id;
  struct s8 method;
  struct json_value params;
};

struct jsonrpc_error {
  int code;
  struct s8 message;
  struct json_value data;
};

struct jsonrpc_notification {
  struct s8 method;
  struct json_value params;
};

struct jsonrpc_response {
  struct json_value id;
  bool ok;
  union jsonrpc_value {
    struct json_value result;
    struct jsonrpc_error error;
  } value;
};

struct jsonrpc_message {
  enum jsonrpc_type type;
  union jsonrpc_msg {
    struct jsonrpc_request request;
    struct jsonrpc_response response;
    struct jsonrpc_notification notification;
  } message;

  struct json_value document;
};

struct jsonrpc_message jsonrpc_parse(const uint8_t *buf, uint64_t size);
struct s8 jsonrpc_format_request(struct json_value id, struct s8 method,
                                 struct s8 params);
struct s8 jsonrpc_format_response(struct json_value id, struct s8 result);
struct s8 jsonrpc_format_notification(struct s8 method, struct s8 params);

#endif

#include "jsonrpc.h"

#include <stddef.h>

struct jsonrpc_request jsonrpc_request_create(struct json_value id,
                                              struct s8 method,
                                              struct json_object *params) {
  return (struct jsonrpc_request){
      .id = id,
      .method = method,
      .params = params,
  };
}

struct jsonrpc_response jsonrpc_parse_response(const uint8_t *buf,
                                               uint64_t size) {
  (void)buf;
  (void)size;
  return (struct jsonrpc_response){};
}

struct s8 jsonrpc_request_to_string(const struct jsonrpc_request *request) {
  (void)request;
  return (struct s8){.l = 0, .s = NULL};
}

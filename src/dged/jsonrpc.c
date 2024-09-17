#include "jsonrpc.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

struct jsonrpc_message jsonrpc_parse(const uint8_t *buf, uint64_t size) {

  struct json_result res = json_parse(buf, size);
  if (!res.ok) {
    return (struct jsonrpc_message){
        .type = Jsonrpc_Response,
        .document = (struct json_value){.type = Json_Null, .parent = NULL},
        .message.response = (struct jsonrpc_response){
            .id = (struct json_value){.type = Json_Null},
            .ok = false,
            .value.error =
                (struct jsonrpc_error){
                    .code = 0,
                    .message = s8(res.result.error),
                },
        }};
  }

  struct json_value doc = res.result.document;
  struct json_object *obj = doc.value.object;

  if (json_contains(obj, s8("error"))) {
    struct json_object *err_obj = json_get(obj, s8("error"))->value.object;
    return (struct jsonrpc_message){
        .type = Jsonrpc_Response,
        .document = doc,
        .message.response =
            (struct jsonrpc_response){
                .id = *json_get(obj, s8("id")),
                .ok = false,
                .value.error =
                    (struct jsonrpc_error){
                        .code = json_get(err_obj, s8("code"))->value.number,
                        .message =
                            json_get(err_obj, s8("message"))->value.string,
                    },
            },
    };
  } else if (!json_contains(obj, s8("id"))) {
    // no id == notification
    return (struct jsonrpc_message){
        .type = Jsonrpc_Notification,
        .document = doc,
        .message.notification =
            (struct jsonrpc_notification){
                .method = json_get(obj, s8("method"))->value.string,
                .params = *json_get(obj, s8("params")),
            },
    };
  } else if (json_contains(obj, s8("method"))) {
    // request
    return (struct jsonrpc_message){
        .type = Jsonrpc_Request,
        .document = doc,
        .message.request = (struct jsonrpc_request){
            .id = *json_get(obj, s8("id")),
            .method = json_get(obj, s8("method"))->value.string,
            .params = *json_get(obj, s8("params")),
        }};
  }

  // response
  return (struct jsonrpc_message){
      .type = Jsonrpc_Response,
      .document = doc,
      .message.response = (struct jsonrpc_response){
          .id = *json_get(obj, s8("id")),
          .ok = true,
          .value.result = *json_get(obj, s8("result")),
      }};
}

struct s8 jsonrpc_format_request(struct json_value id, struct s8 method,
                                 struct s8 params) {
  const char *fmt = "{ \"jsonrpc\": \"2.0\", \"id\": %d, \"method\": \"%.*s\", "
                    "\"params\": %.*s }";
  size_t s = snprintf(NULL, 0, fmt, (int)id.value.number, method.l, method.s,
                      params.l, params.s);
  char *buf = calloc(s + 1, 1);
  snprintf(buf, s + 1, fmt, (int)id.value.number, method.l, method.s, params.l,
           params.s);

  return (struct s8){
      .s = (uint8_t *)buf,
      .l = s,
  };
}

struct s8 jsonrpc_format_response(struct json_value id, struct s8 result) {
  const char *fmt = "{ \"jsonrpc\": \"2.0\", \"id\": %d, \"result\": %.*s }";
  size_t s = snprintf(NULL, 0, fmt, (int)id.value.number, result.l, result.s);
  char *buf = calloc(s + 1, 1);
  snprintf(buf, s + 1, fmt, (int)id.value.number, result.l, result.s);

  return (struct s8){
      .s = (uint8_t *)buf,
      .l = s,
  };
}

struct s8 jsonrpc_format_notification(struct s8 method, struct s8 params) {
  const char *fmt = "{ \"jsonrpc\": \"2.0\", \"method\": \"%.*s\", "
                    "\"params\": %.*s }";
  size_t s = snprintf(NULL, 0, fmt, method.l, method.s, params.l, params.s);
  char *buf = calloc(s + 1, 1);
  snprintf(buf, s + 1, fmt, method.l, method.s, params.l, params.s);

  return (struct s8){
      .s = (uint8_t *)buf,
      .l = s,
  };
}

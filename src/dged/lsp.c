#include "lsp.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "buffer.h"
#include "bufread.h"
#include "jsonrpc.h"
#include "process.h"
#include "reactor.h"

struct pending_write {
  char headers[256];
  uint64_t headers_len;
  uint64_t written;
  struct s8 payload;
};

typedef VEC(struct pending_write) write_vec;

enum read_state {
  Read_Headers,
  Read_Payload,
};

struct lsp {
  const char *name;
  char *const *command;
  struct process *process;
  struct reactor *reactor;
  struct buffer *stderr_buffer;
  uint32_t stdin_event;
  uint32_t stdout_event;
  uint32_t stderr_event;

  write_vec writes;

  enum read_state read_state;
  struct bufread *reader;
  uint8_t header_buffer[4096];
  size_t header_len;
  size_t content_len;
  size_t curr_content_len;
  uint8_t *reader_buffer;
};

struct lsp *lsp_create(char *const command[], struct reactor *reactor,
                       struct buffer *stderr_buffer, const char *name) {
  // check length of command
  if (command == NULL) {
    return NULL;
  }

  uint32_t command_len = 0;
  while (command[command_len] != NULL) {
    ++command_len;
  }

  if (command_len == 0) {
    return NULL;
  }

  struct lsp *lsp = calloc(1, sizeof(struct lsp));

  char **cmd = calloc(command_len + 1, sizeof(const char *));
  memcpy(cmd, command, sizeof(const char *) * command_len);
  cmd[command_len] = NULL;
  lsp->command = cmd;

  if (name != NULL) {
    lsp->name = strdup(name);
  } else {
#ifdef __unix__
    const char *lslash = strrchr(lsp->command[0], '/');
#elif defined(_WIN32) || defined(WIN32)
    const char *lslash = strrchr(lsp->command[0], '\\');
#endif
    if (lslash == NULL) {
      lsp->name = strdup(lsp->command[0]);
    } else {
      lsp->name = strdup(lslash + 1);
    }
  }
  lsp->stderr_buffer = stderr_buffer;
  lsp->reactor = reactor;
  lsp->stdin_event = -1;
  lsp->stdout_event = -1;
  lsp->stderr_event = -1;
  lsp->reader = NULL;
  lsp->read_state = Read_Headers;
  lsp->curr_content_len = 0;

  VEC_INIT(&lsp->writes, 64);
  return lsp;
}

void lsp_destroy(struct lsp *lsp) {
  free((void *)lsp->name);
  if (lsp->process != NULL) {
    free(lsp->process);
  }

  if (lsp->command != NULL) {
    free((void *)lsp->command);
  }

  VEC_DESTROY(&lsp->writes);

  free(lsp);
}

static bool read_headers(struct lsp *lsp) {
  bool prev_was_cr = false;
  while (true) {
    uint8_t b;
    ssize_t res = bufread_read(lsp->reader, &b, 1);

    if (res == -1 || res == 0) {
      return false;
    }

    if (b == '\n' && prev_was_cr && lsp->header_len == 0) {
      // end of headers
      lsp->reader_buffer = calloc(lsp->content_len, 1);
      lsp->curr_content_len = 0;
      lsp->read_state = Read_Payload;

      return true;
    } else if (b == '\n' && prev_was_cr) {
      // end of individual header
      lsp->header_buffer[lsp->header_len] = '\0';

      if (lsp->header_len > 15 &&
          memcmp(lsp->header_buffer, "Content-Length:", 15) == 0) {
        lsp->content_len = atoi((const char *)&lsp->header_buffer[16]);
      }

      lsp->header_len = 0;

      continue;
    }

    prev_was_cr = false;
    if (b == '\r') {
      prev_was_cr = true;
      continue;
    }

    // TODO: handle this case
    if (lsp->header_len < 4096) {
      lsp->header_buffer[lsp->header_len] = b;
      ++lsp->header_len;
    }
  }
}

static bool read_payload(struct lsp *lsp) {
  ssize_t res =
      bufread_read(lsp->reader, &lsp->reader_buffer[lsp->curr_content_len],
                   lsp->content_len - lsp->curr_content_len);

  if (res == -1) {
    return false;
  } else if (res == 0) {
    return false;
  }

  lsp->curr_content_len += res;
  return lsp->curr_content_len == lsp->content_len;
}

static void init_lsp_message(struct lsp_message *lsp_msg, uint8_t *payload,
                             size_t len) {

  lsp_msg->jsonrpc_msg = jsonrpc_parse(payload, len);
  lsp_msg->parsed = true; // this is parsed to json
  lsp_msg->payload.s = payload;
  lsp_msg->payload.l = len;

  switch (lsp_msg->jsonrpc_msg.type) {
  case Jsonrpc_Request: {
    lsp_msg->type = Lsp_Request;
    struct jsonrpc_request *jreq = &lsp_msg->jsonrpc_msg.message.request;
    struct lsp_request *lreq = &lsp_msg->message.request;

    lreq->id = (request_id)jreq->id.value.number;
    lreq->method = jreq->method;
    lreq->params = jreq->params;
  } break;

  case Jsonrpc_Response: {
    lsp_msg->type = Lsp_Response;
    struct jsonrpc_response *jresp = &lsp_msg->jsonrpc_msg.message.response;
    struct lsp_response *lresp = &lsp_msg->message.response;

    lresp->id = (request_id)jresp->id.value.number;
    lresp->ok = jresp->ok;
    if (lresp->ok) {
      lresp->value.result = jresp->value.result;
    } else {
      lresp->value.error.code = jresp->value.error.code;
      lresp->value.error.message = jresp->value.error.message;
      lresp->value.error.data = jresp->value.error.data;
    }
  } break;

  case Jsonrpc_Notification: {
    lsp_msg->type = Lsp_Notification;
    struct jsonrpc_notification *jnot =
        &lsp_msg->jsonrpc_msg.message.notification;
    struct lsp_notification *lnot = &lsp_msg->message.notification;

    lnot->method = jnot->method;
    lnot->params = jnot->params;
  } break;
  }
}

uint32_t lsp_update(struct lsp *lsp, struct lsp_message *msgs,
                    uint32_t nmax_msgs) {
  if (!lsp_server_running(lsp)) {
    return -1;
  }

  uint32_t nmsgs = 0;

  // read stderr
  if (lsp->stderr_event != (uint32_t)-1) {
    uint8_t buf[1024];
    if (reactor_poll_event(lsp->reactor, lsp->stderr_event)) {
      ssize_t nb = 0;
      while ((nb = read(lsp->process->stderr, buf, 1024)) > 0) {
        buffer_set_readonly(lsp->stderr_buffer, false);
        buffer_add(lsp->stderr_buffer, buffer_end(lsp->stderr_buffer), buf, nb);
        buffer_set_readonly(lsp->stderr_buffer, true);
      }
    }
  }

  // write pending requests
  if (reactor_poll_event(lsp->reactor, lsp->stdin_event)) {
    VEC_FOR_EACH(&lsp->writes, struct pending_write * w) {
      ssize_t written = 0;
      ssize_t to_write = 0;

      // write headers first
      if (w->written < w->headers_len) {
        to_write = w->headers_len - w->written;
        written = write(lsp->process->stdin, w->headers + w->written, to_write);
      }

      // did an error occur
      if (written < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
          // TODO: log error somehow
        }
        goto cleanup_writes;
      } else {
        w->written += written;
      }

      // write content next
      if (w->written >= w->headers_len) {
        to_write = w->payload.l + w->headers_len - w->written;
        size_t offset = w->written - w->headers_len;
        written = write(lsp->process->stdin, w->payload.s + offset, to_write);
      }

      // did an error occur
      if (written < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
          // TODO: log error somehow
        }
        goto cleanup_writes;
      } else {
        w->written += written;
      }
    }
  }

cleanup_writes:
  /* lsp->writes = filter(&lsp->writes, x: x.written < x.payload.l +
   * x.headers_len) */
  if (!VEC_EMPTY(&lsp->writes)) {
    write_vec writes = lsp->writes;
    VEC_INIT(&lsp->writes, VEC_CAPACITY(&writes));

    VEC_FOR_EACH(&writes, struct pending_write * w) {
      if (w->written < w->payload.l + w->headers_len) {
        // copying 256 bytes, goodbye vaccuum tubes...
        VEC_PUSH(&lsp->writes, *w);
      } else {
        s8delete(w->payload);
      }
    }
    VEC_DESTROY(&writes);
  }

  if (VEC_EMPTY(&lsp->writes)) {
    reactor_unregister_interest(lsp->reactor, lsp->stdin_event);
    lsp->stdin_event = (uint32_t)-1;
  }

  // process incoming messages
  // TODO: handle the case where we might leave data
  if (reactor_poll_event(lsp->reactor, lsp->stdout_event)) {
    bool has_data = true;
    while (has_data) {
      switch (lsp->read_state) {
      case Read_Headers:
        has_data = read_headers(lsp);
        break;
      case Read_Payload: {
        bool payload_ready = read_payload(lsp);
        if (payload_ready) {
          if (nmsgs == nmax_msgs) {
            return nmsgs;
          }

          init_lsp_message(&msgs[nmsgs], lsp->reader_buffer, lsp->content_len);
          ++nmsgs;

          // set up for next message
          lsp->reader_buffer = NULL;
          lsp->content_len = 0;
          lsp->read_state = Read_Headers;
        }

        // it only returns if we are out of data or if
        // the payload is ready
        has_data = payload_ready;
        break;
      }
      }
    }
  }

  return nmsgs;
}

int lsp_start_server(struct lsp *lsp) {
  struct process p;
  struct process_create_result res = process_create(lsp->command, &p);

  if (!res.ok) {
    // TODO: losing error message here
    return -1;
  }

  lsp->process = calloc(1, sizeof(struct process));
  memcpy(lsp->process, &p, sizeof(struct process));

  lsp->stdout_event = reactor_register_interest(
      lsp->reactor, lsp->process->stdout, ReadInterest);

  if (lsp->stdout_event == (uint32_t)-1) {
    return -3;
  }

  lsp->stderr_event = reactor_register_interest(
      lsp->reactor, lsp->process->stderr, ReadInterest);

  lsp->reader = bufread_create(lsp->process->stdout, 8192);

  return 0;
}

int lsp_restart_server(struct lsp *lsp) {
  lsp_stop_server(lsp);
  return lsp_start_server(lsp);
}

void lsp_stop_server(struct lsp *lsp) {
  if (lsp->stderr_event != (uint32_t)-1) {
    reactor_unregister_interest(lsp->reactor, lsp->stderr_event);
    lsp->stderr_event = (uint32_t)-1;
  }

  if (lsp->stdin_event != (uint32_t)-1) {
    reactor_unregister_interest(lsp->reactor, lsp->stdin_event);
    lsp->stdin_event = (uint32_t)-1;
  }

  if (lsp->stdout_event != (uint32_t)-1) {
    reactor_unregister_interest(lsp->reactor, lsp->stdout_event);
    lsp->stdout_event = (uint32_t)-1;
  }

  if (lsp_server_running(lsp)) {
    process_kill(lsp->process);
  }

  if (lsp->process != NULL) {
    process_destroy(lsp->process);
    free(lsp->process);
    lsp->process = NULL;
  }

  if (lsp->reader != NULL) {
    bufread_destroy(lsp->reader);
    lsp->reader = NULL;
  }
}

bool lsp_server_running(const struct lsp *lsp) {
  return lsp->process != NULL ? process_running(lsp->process) : false;
}

uint64_t lsp_server_pid(const struct lsp *lsp) {
  if (!lsp_server_running(lsp)) {
    return -1;
  }

  return lsp->process->id;
}

const char *lsp_server_name(const struct lsp *lsp) { return lsp->name; }

struct lsp_message lsp_create_request(request_id id, struct s8 method,
                                      struct s8 payload) {
  struct lsp_message msg = {
      .type = Lsp_Request,
      .parsed = false, // payload is raw
      .message.request.method = method,
      .message.request.id = id,
      .payload = jsonrpc_format_request(
          (struct json_value){
              .type = Json_Number,
              .value.number = (double)id,
              .parent = NULL,
          },
          method, payload.l > 0 ? payload : s8("{}")),
  };

  return msg;
}

struct lsp_message lsp_create_response(request_id id, bool ok,
                                       struct s8 payload) {
  struct lsp_message msg = {
      .type = Lsp_Response,
      .parsed = false, // payload is raw
      .message.response.ok = ok,
      .message.response.id = id,
      .payload = jsonrpc_format_response(
          (struct json_value){
              .type = Json_Number,
              .value.number = (double)id,
              .parent = NULL,
          },
          payload.l > 0 ? payload : s8("{}")),
  };

  return msg;
}

struct lsp_message lsp_create_notification(struct s8 method,
                                           struct s8 payload) {
  struct lsp_message msg = {
      .type = Lsp_Notification,
      .parsed = false, // payload is raw
      .message.notification.method = method,
      .payload = jsonrpc_format_notification(method, payload.l > 0 ? payload
                                                                   : s8("{}")),
  };

  return msg;
}

void lsp_send(struct lsp *lsp, struct lsp_message message) {

  VEC_APPEND(&lsp->writes, struct pending_write * w);
  w->headers_len = snprintf(w->headers, 256, "Content-Length: %d\r\n\r\n",
                            message.payload.l);
  w->payload = message.payload;
  w->written = 0;

  if (lsp->stdin_event == (uint32_t)-1) {
    lsp->stdin_event = reactor_register_interest(
        lsp->reactor, lsp->process->stdin, WriteInterest);
  }
}

void lsp_message_destroy(struct lsp_message *message) {
  if (message->parsed) {
    json_destroy(&message->jsonrpc_msg.document);
  }
  s8delete(message->payload);
}

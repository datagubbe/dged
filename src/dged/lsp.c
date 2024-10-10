#include "lsp.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "buffer.h"
#include "jsonrpc.h"
#include "process.h"
#include "reactor.h"

struct pending_write {
  char headers[256];
  uint64_t headers_len;
  uint64_t request_id;
  uint64_t written;
  struct s8 payload;
};

struct pending_read {
  uint64_t request_id;
  struct s8 payload;
};

typedef VEC(struct pending_write) write_vec;

struct lsp {
  const char *name;
  char *const *command;
  struct process *process;
  struct reactor *reactor;
  struct buffer *stderr_buffer;
  struct lsp_client client_impl;
  uint32_t stdin_event;
  uint32_t stdout_event;
  uint32_t stderr_event;

  request_id current_id;

  write_vec writes;
  VEC(struct pending_read) reads;
};

struct lsp *lsp_create(char *const command[], struct reactor *reactor,
                       struct buffer *stderr_buffer,
                       struct lsp_client client_impl, const char *name) {
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
  lsp->client_impl = client_impl;
  lsp->reactor = reactor;
  lsp->stdin_event = -1;
  lsp->stdout_event = -1;
  lsp->stderr_event = -1;
  lsp->current_id = 0;

  return lsp;
}

void lsp_destroy(struct lsp *lsp) {
  free((void *)lsp->name);
  if (lsp->process != NULL) {
    free(lsp->process);
  }
  if (lsp->command != NULL) {
    char *const *command = &lsp->command[0];
    while (command != NULL) {
      free(*command);
      ++command;
    }

    free((void *)lsp->command);
  }
  free(lsp);
}

uint32_t lsp_update(struct lsp *lsp, struct lsp_response **responses,
                    uint32_t responses_capacity) {

  (void)responses;
  (void)responses_capacity;

  if (!lsp_server_running(lsp)) {
    return -1;
  }

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
      size_t written = 0;
      uint64_t to_write = 0;
      if (w->written < w->headers_len) {
        to_write = w->headers_len - w->written;
        written = write(lsp->process->stdin, w->headers + w->written, to_write);
      } else {
        to_write = w->payload.l + w->headers_len - w->written;
        written = write(lsp->process->stdin, w->payload.s, to_write);

        if (to_write == written) {
          VEC_APPEND(&lsp->reads, struct pending_read * r);
          r->request_id = w->request_id;
        }
      }

      w->written += written;

      // if this happens, we ran out of buffer space
      if (written < to_write) {
        goto cleanup_writes;
      }
    }
  }

cleanup_writes:
  /* lsp->writes = filter(&lsp->writes, x: x.written < x.payload.l +
   * x.headers_len) */
  write_vec writes = lsp->writes;
  VEC_INIT(&lsp->writes, VEC_SIZE(&writes));

  VEC_FOR_EACH(&writes, struct pending_write * w) {
    if (w->written < w->payload.l + w->headers_len) {
      // copying 256 bytes, goodbye vaccuum tubes...
      VEC_PUSH(&lsp->writes, *w);
    }
  }
  VEC_DESTROY(&writes);

  // TODO: process incoming responses

  return 0;
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
  lsp->stderr_event = reactor_register_interest(
      lsp->reactor, lsp->process->stderr, ReadInterest);
  lsp->stdin_event = reactor_register_interest(
      lsp->reactor, lsp->process->stdin, WriteInterest);

  if (lsp->stdin_event == (uint32_t)-1) {
    return -2;
  }

  return 0;
}

int lsp_restart_server(struct lsp *lsp) {
  if (lsp_server_running(lsp)) {
    lsp_stop_server(lsp);
  }

  return lsp_start_server(lsp);
}

void lsp_stop_server(struct lsp *lsp) {
  process_kill(lsp->process);
  process_destroy(lsp->process);
  free(lsp->process);
  lsp->process = NULL;
}

bool lsp_server_running(const struct lsp *lsp) {
  if (lsp->process == NULL) {
    return false;
  }

  return process_running(lsp->process);
}

uint64_t lsp_server_pid(const struct lsp *lsp) {
  if (!lsp_server_running(lsp)) {
    return -1;
  }

  return lsp->process->id;
}

const char *lsp_server_name(const struct lsp *lsp) { return lsp->name; }

request_id lsp_request(struct lsp *lsp, struct lsp_request request) {
  struct json_value js_id = {
      .type = Json_Number,
      .value.number = (double)lsp->current_id,
      .parent = NULL,
  };

  struct jsonrpc_request req =
      jsonrpc_request_create(js_id, request.method, request.params);
  struct s8 payload = jsonrpc_request_to_string(&req);

  VEC_APPEND(&lsp->writes, struct pending_write * w);
  w->headers_len =
      snprintf(w->headers, 256, "Content-Length: %d\r\n\r\n", payload.l);
  w->request_id = lsp->current_id;
  w->payload = payload;

  ++lsp->current_id;

  return w->request_id;
}

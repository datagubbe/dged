#include "lsp.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "buffer.h"
#include "process.h"
#include "reactor.h"

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

  return lsp;
}

void lsp_destroy(struct lsp *lsp) {
  free((void *)lsp->name);
  if (lsp->process != NULL) {
    free(lsp->process);
  }
  if (lsp->command != NULL) {
    char *command = lsp->command[0];
    while (command != NULL) {
      free(command);
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

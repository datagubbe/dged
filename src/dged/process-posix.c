#include "process.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/wait.h>

static int create_pipe(int *read_end, int *write_end, bool read_nonblock,
                       bool write_nonblock) {
  int pipes[2] = {0};
  if (pipe(pipes) < 0) {
    return -1;
  }

  if (write_nonblock) {
    int flags = fcntl(pipes[1], F_GETFL, 0);
    if (flags < 0) {
      return -1;
    }

    flags |= O_NONBLOCK;
    if (fcntl(pipes[1], F_SETFL, flags) < 0) {
      return -1;
    }
  }

  if (read_nonblock) {
    int flags = fcntl(pipes[0], F_GETFL, 0);
    if (flags < 0) {
      return -1;
    }

    flags |= O_NONBLOCK;
    if (fcntl(pipes[0], F_SETFL, flags) < 0) {
      return -1;
    }
  }

  *read_end = pipes[0];
  *write_end = pipes[1];

  return 0;
}

struct process_create_result process_create(char *const command[],
                                            struct process *result) {

  int stdin_read, stdin_write;
  if (create_pipe(&stdin_read, &stdin_write, false, true) < 0) {
    return (struct process_create_result){
        .ok = false,
        .error_message = strerror(errno),
    };
  }

  int stdout_read, stdout_write;
  if (create_pipe(&stdout_read, &stdout_write, true, false) < 0) {
    return (struct process_create_result){
        .ok = false,
        .error_message = strerror(errno),
    };
  }

  int stderr_read, stderr_write;
  if (create_pipe(&stderr_read, &stderr_write, true, false) < 0) {
    return (struct process_create_result){
        .ok = false,
        .error_message = strerror(errno),
    };
  }

  pid_t pid = fork();
  if (pid == -1) {
    return (struct process_create_result){
        .ok = false,
        .error_message = strerror(errno),
    };
  } else if (pid == 0) {
    close(stdin_write);
    close(stdout_read);
    close(stderr_read);

    if (dup2(stdin_read, STDIN_FILENO) < 0) {
      exit(16);
    }

    if (dup2(stdout_write, STDOUT_FILENO) < 0) {
      exit(16);
    }

    if (dup2(stderr_write, STDERR_FILENO) < 0) {
      exit(16);
    }

    if (execvp(command[0], command) < 0) {
      exit(16);
    }
  } else {
    close(stdin_read);
    close(stdout_write);
    close(stderr_write);

    result->stdin = stdin_write;
    result->stdout = stdout_read;
    result->stderr = stderr_read;
    result->id = (fd_t)pid;
    result->impl = NULL;
  }

  return (struct process_create_result){
      .ok = true,
  };
}

void process_destroy(struct process *p) { (void)p; }

bool process_running(const struct process *p) {
  return waitpid(p->id, NULL, WNOHANG) == 0;
}

bool process_kill(const struct process *p) { return kill(p->id, SIGTERM) == 0; }

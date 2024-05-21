#ifndef _PROCESS_H
#define _PROCESS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef _WIN32
typedef HANDLE fd_t;
#else
typedef int fd_t;
#endif

struct platform_process;
struct process {
  uint64_t id;
  fd_t stdin;
  fd_t stdout;
  fd_t stderr;
  struct platform_process *impl;
};

struct process_create_result {
  bool ok;
  const char *error_message;
};

struct process_create_result process_create(char *const command[],
                                            struct process *result);

void process_destroy(struct process *p);

bool process_running(const struct process *p);
bool process_kill(const struct process *p);

#endif

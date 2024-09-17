#include "assert.h"

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void assert_internal(bool cond, const char *cond_str, const char *file,
                            int line, const char *msg, va_list args) {
  if (!cond) {
    va_list args2;
    va_copy(args2, args);

    ssize_t res = vsnprintf(NULL, 0, msg, args);
    char *buf = (char *)msg;

    if (res != -1) {
      buf = malloc(res + 1);
      vsnprintf(buf, res + 1, msg, args2);
    }

    va_end(args);

    printf("\n%s:%d: assert failed (%s): %s\n", file, line, cond_str, buf);

    if (buf != msg) {
      free(buf);
    }
    raise(SIGABRT);
  }
}

void assert(bool cond, const char *cond_str, const char *file, int line,
            const char *msg, ...) {
  va_list args;
  va_start(args, msg);
  assert_internal(cond, cond_str, file, line, msg, args);
}

void assert_streq(const char *left, const char *right, const char *file,
                  int line, const char *msg, ...) {
  va_list args;
  va_start(args, msg);
  assert_internal(strcmp(left, right) == 0, "<left string> == <right string>",
                  file, line, msg, args);
}

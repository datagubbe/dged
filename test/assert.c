#include "assert.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void assert(bool cond, const char *cond_str, const char *file, int line,
            const char *msg) {
  if (!cond) {
    printf("\n%s:%d: assert failed (%s): %s\n", file, line, cond_str, msg);
    raise(SIGABRT);
  }
}

void assert_streq(const char *left, const char *right, const char *file,
                  int line, const char *msg) {
  assert(strcmp(left, right) == 0, "<left string> == <right string>", file,
         line, msg);
}

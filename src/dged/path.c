#include "path.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

char *expanduser(const char *path) {
  // replace tilde
  char *res = NULL;
  char *tilde_pos = strchr(path, '~');
  if (tilde_pos != NULL) {
    char *home = getenv("HOME");
    if (home != NULL) {
      // allocate a new string based with the new len
      size_t home_len = strlen(home);
      size_t path_len = strlen(path);
      size_t total_len = path_len + home_len;
      res = malloc(total_len);
      size_t initial_len = tilde_pos - path;
      strncpy(res, path, initial_len);

      strncpy(res + initial_len, home, home_len + 1);

      size_t rest_len = path_len - initial_len - 1;
      strncpy(res + initial_len + home_len, path + initial_len + 1, rest_len);
      res[total_len - 1] = '\0';
    }
  }

  return res != NULL ? res : strdup(path);
}

char *to_abspath(const char *path) {
  char *exp = expanduser(path);
  char *p = realpath(path, NULL);
  if (p != NULL) {
    free(exp);
    return p;
  } else {
    return exp;
  }
}

const char *join_path_with_delim(const char *p1, const char *p2,
                                 const char delim) {
  size_t len1 = strlen(p1);
  size_t len2 = strlen(p2);

  char *path = malloc(len1 + len2 + 2);
  uint32_t idx = 0;
  memcpy(&path[idx], p1, len1);
  idx += len1;
  path[idx++] = delim;
  memcpy(&path[idx], p2, len2);
  idx += len2;
  path[idx++] = '\0';

  return path;
}

const char *join_path(const char *p1, const char *p2) {
#ifdef __unix__
  return join_path_with_delim(p1, p2, '/');
#elif defined(_WIN32) || defined(WIN32)
  return join_path_with_delim(p1, p2, '\\');
#endif
}

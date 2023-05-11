#include <limits.h>
#include <stdlib.h>
#include <string.h>

static char *expanduser(const char *path) {
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

        strncpy(res + initial_len, home, home_len);

        size_t rest_len = path_len - initial_len - 1;
        strncpy(res + initial_len + home_len, path + initial_len + 1, rest_len);
        res[total_len-1] = '\0';
    }
  }

  return res != NULL ? res : strdup(path);
}

static char *to_abspath(const char *path) {
  char *p = realpath(path, NULL);
  if (p != NULL) {
    return p;
  } else {
    return strdup(path);
  }
}

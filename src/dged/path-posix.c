#include "path.h"

#include "s8.h"

#include <sys/stat.h>
#include <unistd.h>

bool path_exists(struct s8 path) { return access(s8ascstr(path), F_OK) == 0; }

struct s8 working_dir() {
  const char *cwd = getcwd(NULL, 0);
  if (cwd == NULL) {
    return (struct s8){.s = NULL, .l = 0};
  }

  return s8(cwd);
}

bool create_directories(struct s8 path) {
  struct s8 *segments = NULL;
  size_t nsegments = path_segments(path, &segments);

  if (nsegments == 0) {
    return true;
  }

  for (size_t i = 0; i < nsegments; ++i) {
    struct s8 path = join_path_segments(segments, i + 1);
    if (!path_exists(path)) {
      if (mkdir(s8ascstr(path), 0777) == -1) {
        s8delete(path);
        free_path_segments(segments, nsegments);
        return false;
      }
    }

    s8delete(path);
  }

  free_path_segments(segments, nsegments);
  return true;
}

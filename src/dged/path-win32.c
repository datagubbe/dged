#include "path.h"

#include "s8.h"

#include <stdint.h>
#include <stdlib.h>
#include <wchar.h>

struct security_attributes {
  uint32_t lenght;
  void *security_descriptor;
  bool inherit_handle;
};

uint32_t GetFileAttributes(wchar_t *path);
uint32_t GetCurrentDirectory(uint32_t buflen, wchar_t *res);
bool CreateDirectory(wchar_t *path_name, struct security_attributes *security_attributes);

bool path_exists(struct s8 path) {
  size_t len = mbstowcs(NULL, s8ascstr(path), 0);
  wchar_t *lpath = malloc(len+1);

  mbstowcs(lpath, s8ascstr(path), len);

  uint32_t attributes =  GetFileAttributes(lpath);
  free(lpath);
  return attributes != (uint32_t)-1;
}

struct s8 working_dir() {
  wchar_t cwd[4096];

  if (GetCurrentDirectory(4096, cwd) == 0) {
    return (struct s8){.s = NULL, .l = 0};
  }

  size_t len = wcstombs(NULL, cwd, NULL);
  char *mbpath = malloc(len+1);
  wcstombs(mbpath, cwd, len);
  return (struct s8){.s = (uint8_t*) mbpath, .l = len};
}

static bool create_dir(struct s8 path) {
  size_t len = mbstowcs(NULL, s8ascstr(path), 0);
  wchar_t *lpath = malloc(len+1);

  mbstowcs(lpath, s8ascstr(path), len);

  bool res = CreateDirectory(lpath, NULL);
  free(lpath);
  return res;
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
      if (!create_dir(path)) {
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

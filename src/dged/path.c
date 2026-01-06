#include "path.h"

#include "dirent.h"
#include "s8.h"
#include "sys/stat.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct s8 expanduser(struct s8 path) {
  // replace tilde
  struct s8 res = {};
  ssize_t tilde_pos = s8find(path, '~');
  if (tilde_pos != -1) {
    char *home = getenv("HOME");
    if (home != NULL) {
      size_t remainder_len = path.l - tilde_pos;
      res = s8from_fmt("%.*s%s%.*s", tilde_pos, path.s, home, remainder_len,
                       path.s + tilde_pos + 1);
    }
  } else {
    res = s8dup(path);
  }

  return res;
}

struct s8 unexpanduser(struct s8 path) { return s8dup(path); }

struct s8 working_dir() {
#if defined(_WIN32)
#error "implement"
#else
  const char *cwd = getcwd(NULL, 0);
  if (cwd == NULL) {
    return (struct s8){.s = NULL, .l = 0};
  }

  return s8(cwd);
#endif
}

struct s8 canonicalize(struct s8 path) {
  struct s8 exp = expanduser(path);
  struct s8 absolute = exp;

  if (!is_absolute(absolute)) {
    struct s8 cwd = working_dir();
    absolute = join_path(cwd, exp);

    s8delete(cwd);
    s8delete(exp);
  }

  if (!path_exists(absolute)) {
    // if the path does not exist, do not
    // resolve any symlinks
    return absolute;
  }

  const char *resolved = realpath(s8ascstr(absolute), NULL);
  s8delete(absolute);

  return s8(resolved);
}

bool is_absolute(struct s8 path) {
#if defined(_WIN32)
#error "implement"
#else
  return s8startswith(path, PATHSEP) || s8startswith(path, s8("~"));
#endif
}

bool is_relative(struct s8 path) { return !is_absolute(path); }

struct s8 join_path_with_delim(struct s8 p1, struct s8 p2, const char delim) {
  return s8from_fmt("%s%c%s", s8ascstr(p1), delim, s8ascstr(p2));
}

struct s8 join_path(struct s8 p1, struct s8 p2) {
#ifdef __unix__
  return join_path_with_delim(p1, p2, '/');
#elif defined(_WIN32) || defined(WIN32)
  return join_path_with_delim(p1, p2, '\\');
#endif
}

struct s8 join_path_segments(struct s8 *segments, size_t nsegments) {
#if defined(_WIN32)
  const char pathsep = '\\';
#else
  const char pathsep = '/';
#endif

  size_t orig_len = 0;
  if (nsegments > 1 && segments[0].l == 1 && s8at(segments[0], 0) == pathsep) {
    orig_len = segments[0].l;
    segments[0].l = 0;
  }

  struct s8 joined = s8join(segments, nsegments, pathsep);

  if (orig_len > 0) {
    segments[0].l = orig_len;
  }

  return joined;
}

void free_path_segments(struct s8 *segments, size_t nsegments) {
  for (size_t i = 0; i < nsegments; ++i) {
    s8delete(segments[i]);
  }

  free(segments);
}

static bool is_valid_segment(struct s8 path, size_t start, size_t end) {
  if (end - start == 0) {
    return false;
  }

  if (end - start == 1 && s8at(path, start) == '.') {
    return false;
  }

  return true;
}

size_t path_segments(struct s8 path, struct s8 **segments) {
#if defined(_WIN32)
  const char pathsep = '\\';
#else
  const char pathsep = '/';
#endif

  if (s8empty(path)) {
    *segments = NULL;
    return 0;
  }

  size_t nsegments = s8startswith(path, PATHSEP) ? 1 : 0, start = 0;
  for (size_t i = 0; i < path.l; ++i) {
    if (path.s[i] == pathsep) {
      if (is_valid_segment(path, start, i)) {
        ++nsegments;
      }

      start = i + 1;
    }
  }

  if (is_valid_segment(path, start, path.l)) {
    nsegments++;
  }

  struct s8 *res = calloc(nsegments, sizeof(struct s8));

  start = 0;
  size_t segi = 0;

  if (s8at(path, 0) == pathsep) {
    res[segi] = s8dup(PATHSEP);
    ++segi;
    ++start;
  }

  for (size_t i = start; i < path.l; ++i) {
    if (path.s[i] == pathsep) {
      if (is_valid_segment(path, start, i)) {
        res[segi] = s8substr(path, start, i);
        ++segi;
      }

      start = i + 1;
    }
  }

  if (is_valid_segment(path, start, path.l)) {
    res[segi] = s8substr(path, start, path.l);
    ++segi;
  }

  *segments = res;
  return nsegments;
}

bool path_exists(struct s8 path) {
#if defined(_WIN32)
#error "implement"
#else
  return access(s8ascstr(path), F_OK) == 0;
#endif
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

struct s8 parent(struct s8 path) {
  struct s8 *segments = NULL;
  size_t nsegments = path_segments(path, &segments);

  if (nsegments < 2) {
    free_path_segments(segments, nsegments);
    return s8dup(path);
  }

  struct s8 ret = join_path_segments(segments, nsegments - 1);
  free_path_segments(segments, nsegments);

  return ret;
}

struct s8 filename(struct s8 path) {
  struct s8 *segments = NULL;
  size_t nsegments = path_segments(path, &segments);

  if (nsegments < 2) {
    free_path_segments(segments, nsegments);
    return (struct s8){.l = 0, .s = NULL};
  }

  struct s8 filename = s8dup(segments[nsegments - 1]);
  free_path_segments(segments, nsegments);
  return filename;
}

struct s8 relative_to(struct s8 path, struct s8 root) {
  if (s8empty(path) || s8empty(root)) {
    return path;
  }

  if (!is_absolute(path) || !is_absolute(root)) {
    return s8dup(path);
  }

  if (!s8startswith(path, root)) {
    return s8dup(path);
  }

  size_t start = s8endswith(root, PATHSEP) ? root.l : root.l + 1;
  return s8substr(path, start, path.l);
}

struct s8 extension(struct s8 path) {
  struct s8 name = filename(path);

  ssize_t ind = s8find(name, '.');
  struct s8 ext = {.l = 0, .s = NULL};
  if (ind != -1) {
    ext = s8substr(name, ind, name.l);
  }

  s8delete(name);
  return ext;
}

struct s8 filestem(struct s8 path) {
  struct s8 name = filename(path);

  ssize_t ind = s8find(name, '.');
  if (ind == -1) {
    return name;
  }

  struct s8 stem = s8substr(name, 0, ind);
  s8delete(name);
  return stem;
}

bool is_link(struct s8 path) {
  struct stat sb;
  int ret = lstat(s8ascstr(path), &sb);

  if (ret == -1) {
    return false;
  }

  return S_ISLNK(sb.st_mode);
}

bool is_dir(struct s8 path) {
  struct stat sb;
  int ret = lstat(s8ascstr(path), &sb);

  if (ret == -1) {
    return false;
  }

  return S_ISDIR(sb.st_mode);
}

bool is_file(struct s8 path) {
  struct stat sb;
  int ret = lstat(s8ascstr(path), &sb);

  if (ret == -1) {
    return false;
  }

  return S_ISREG(sb.st_mode);
}

bool remove_recursive(struct s8 path) {
  if (!path_exists(path)) {
    return false;
  }

  // if it's a symlink or not a directory, unlink it
  if (!is_dir(path) || is_link(path)) {
    if (unlink(s8ascstr(path)) != 0) {
      return false;
    }

    return true;
  }

  DIR *dir = opendir(s8ascstr(path));
  if (dir == NULL) {
    return false;
  }

  bool ret = true;
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;

    struct s8 childpath = join_path(path, s8(entry->d_name));
    if (remove_recursive(childpath) != 0) {
      ret = false;
    }
    s8delete(childpath);
  }

  closedir(dir);

  if (rmdir(s8ascstr(path)) == -1) {
    return false;
  }

  return ret;
}

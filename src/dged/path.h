#ifndef _PATH_H
#define _PATH_H

#include <stdbool.h>
#include <stddef.h>

#if defined(USE_WINDOWS_PATHS)
#define PATHSEP s8("\\")
#elif defined(USE_POSIX_PATHS)
#define PATHSEP s8("/")
#elif defined(_WIN32)
#define PATHSEP s8("\\")
#else
#define PATHSEP s8("/")
#endif

/* pure path functions (do not modify the fs) */
struct s8 expanduser(struct s8 path);
struct s8 unexpanduser(struct s8 path);

struct s8 relative_to(struct s8 path, struct s8 root);

bool is_absolute(struct s8 path);
bool is_relative(struct s8 path);

struct s8 parent(struct s8 path);
struct s8 filename(struct s8 path);
struct s8 extension(struct s8 path);
struct s8 filestem(struct s8 path);

size_t path_segments(struct s8 path, struct s8 **segments);
void free_path_segments(struct s8 *segments, size_t nsegments);

struct s8 join_path_with_delim(struct s8 p1, struct s8 p2, const char delim);
struct s8 join_path(struct s8 p1, struct s8 p2);
struct s8 join_path_segments(struct s8 *segments, size_t nsegments);

/* impure path functions (uses real fs) */
struct s8 canonicalize(struct s8 path);
bool path_exists(struct s8 path);
bool create_directories(struct s8 path);
struct s8 working_dir();

#endif

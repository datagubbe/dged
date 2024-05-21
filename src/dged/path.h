#ifndef _PATH_H
#define _PATH_H

char *expanduser(const char *path);
char *to_abspath(const char *path);
const char *join_path_with_delim(const char *p1, const char *p2,
                                 const char delim);
const char *join_path(const char *p1, const char *p2);

#endif

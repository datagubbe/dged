#ifndef _BUFREAD_H
#define _BUFREAD_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

struct bufread;
struct bufread *bufread_create(int fd, size_t capacity);
void bufread_destroy(struct bufread *br);
ssize_t bufread_read(struct bufread *br, uint8_t *buf, size_t count);

#endif

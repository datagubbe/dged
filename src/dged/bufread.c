#include "bufread.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct bufread {
  uint8_t *buf;
  size_t capacity;
  size_t read_pos;
  size_t write_pos;
  int fd;
  bool empty;
};

struct bufread *bufread_create(int fd, size_t capacity) {
  struct bufread *br = (struct bufread *)calloc(1, sizeof(struct bufread));
  br->buf = calloc(capacity, 1);
  br->capacity = capacity;
  br->read_pos = 0;
  br->write_pos = 0;
  br->empty = true;
  br->fd = fd;

  return br;
}

void bufread_destroy(struct bufread *br) {
  free(br->buf);
  br->buf = NULL;
  br->capacity = 0;
  br->read_pos = 0;
  br->write_pos = 0;
  br->empty = true;
  br->fd = -1;

  free(br);
}

static size_t available(struct bufread *br) {
  if (br->write_pos > br->read_pos) {
    return br->write_pos - br->read_pos;
  } else if (br->write_pos < br->read_pos) {
    return br->write_pos + (br->capacity - br->read_pos);
  }

  /* read == write, either empty or full */
  return br->empty ? 0 : br->capacity;
}

static ssize_t fill(struct bufread *br) {
  ssize_t ret = 0, rd = 0;

  // special case for empty ring buffer
  // in this case, reset read and write pos to beginning
  // and read as much as possible.
  if (br->empty) {
    br->read_pos = 0;
    br->write_pos = 0;

    if ((ret = read(br->fd, br->buf, br->capacity)) <= 0) {
      return ret;
    }

    br->write_pos = ret % br->capacity;
    br->empty = false;
    return ret;
  }

  // first fill the space we have left, either up until the
  // read position or up until the end of the buffer
  size_t end_pos = br->read_pos < br->write_pos ? br->capacity : br->read_pos;
  size_t to_read = end_pos - br->write_pos;

  if (to_read > 0) {
    if ((ret = read(br->fd, &br->buf[br->write_pos], to_read)) < 0) {
      return ret;
    }

    br->write_pos = (br->write_pos + ret) % br->capacity;
    rd += ret;
  }

  // if we filled up until the end, there might
  // be more space at the front
  if (br->write_pos == 0 && br->read_pos > 0) {
    if ((ret = read(br->fd, &br->buf[0], br->read_pos)) < 0) {
      return ret;
    }

    br->write_pos += ret;
    rd += ret;
  }

  return rd;
}

static void consume(struct bufread *br, size_t amount) {
  if (amount >= available(br)) {
    br->empty = true;
    br->read_pos = br->write_pos;
    return;
  }

  br->read_pos = (br->read_pos + amount) % br->capacity;
}

ssize_t bufread_read(struct bufread *br, uint8_t *buf, size_t count) {
  if (count == 0) {
    return 0;
  }

  /* For read requests larger than the internal buffer
   * and an empty internal buffer, go to the
   * underlying source.
   */
  if (br->empty && count >= br->capacity) {
    return read(br->fd, buf, count);
  }

  // make an effort to fill up the buffer
  if (available(br) < count && available(br) < br->capacity) {
    ssize_t fill_res = 0;
    if ((fill_res = fill(br)) < 0) {
      return fill_res;
    }
  }

  // if we get here and we are still empty,
  // all hope is lost...
  if (br->empty) {
    return 0;
  }

  // read (at most) to end
  uint8_t *tgt = buf;
  size_t to_read = 0, rd = 0;
  to_read = (br->read_pos < br->write_pos ? br->write_pos : br->capacity) -
            br->read_pos;
  to_read = to_read > count ? count : to_read;

  memcpy(tgt, &br->buf[br->read_pos], to_read);
  tgt += to_read;
  rd += to_read;
  consume(br, to_read);

  // did we wrap around and have things left to read?
  if (br->read_pos == 0 && !br->empty && rd < count) {
    size_t remaining = count - rd;
    to_read = br->write_pos < remaining ? br->write_pos : remaining;

    memcpy(tgt, br->buf, to_read);
    tgt += to_read;
    rd += to_read;
    consume(br, to_read);
  }

  return rd;
}

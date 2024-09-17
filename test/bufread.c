#ifdef LINUX
#define _GNU_SOURCE
#endif

#include "assert.h"
#include "test.h"

#include "dged/bufread.h"

#include <stdint.h>
#include <unistd.h>

#ifdef LINUX
#include <sys/mman.h>
#endif

static void test_read(void) {
#ifdef LINUX
  int memfd = memfd_create("bufread-test", 0);
  ASSERT(memfd >= 0, "Failed to create memfd");
#endif
  for (int i = 0; i < 256; ++i) {
    int a = write(memfd, (uint8_t *)&i, 1);
    (void)a;
  }
  lseek(memfd, 0, SEEK_SET);

  struct bufread *br = bufread_create(memfd, 128);
  uint8_t buf[32];
  ssize_t read = bufread_read(br, buf, 32);
  ASSERT(read > 0, "Expected to be able to read");
  for (int i = 0; i < 32; ++i) {
    ASSERT(i == buf[i], "Expected buffer to be monotonically increasing");
  }
  bufread_read(br, buf, 32);
  bufread_read(br, buf, 32);
  bufread_read(br, buf, 32);

  read = bufread_read(br, buf, 32);
  ASSERT(read > 0, "Expected to be able to read");
  for (int i = 0; i < 32; ++i) {
    ASSERT((i + 128) == buf[i],
           "Expected buffer to be monotonically increasing");
  }
  bufread_destroy(br);
}

void test_empty_read(void) {
#ifdef LINUX
  int memfd = memfd_create("bufread-test", 0);
  ASSERT(memfd >= 0, "Failed to create memfd");
#endif
  struct bufread *br = bufread_create(memfd, 128);
  uint8_t buf[32];
  ssize_t read = bufread_read(br, buf, 32);
  ASSERT(read == 0, "Expected to not be able to read from empty stream");
  bufread_destroy(br);
}

void run_bufread_tests(void) {
  run_test(test_read);
  run_test(test_empty_read);
}

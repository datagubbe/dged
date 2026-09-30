#ifdef LINUX
#define _GNU_SOURCE
#endif

#include "assert.h"
#include "test.h"

#include "dged/bufread.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
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
  close(memfd);

#ifdef LINUX
  memfd = memfd_create("bufread-test", 0);
  ASSERT(memfd >= 0, "Failed to create memfd");
#endif

  int a = write(memfd, "abcdefghijklmn", 14);
  (void)a;
  lseek(memfd, 0, SEEK_SET);

  uint8_t buf2[9] = {};
  br = bufread_create(memfd, 8);

  ASSERT(bufread_read(br, buf2, 8) == 8,
         "Expected first read of 8 to return 8.");
  ASSERT(memcmp(buf2, "abcdefgh", 8) == 0, "Expected to get abcd back");

  ASSERT(bufread_read(br, buf2, 2) == 2, "Expected read of 2 to return 2.");
  ASSERT(memcmp(buf2, "ij", 2) == 0, "Expected to get ij back");

  ASSERT(bufread_read(br, buf2, 6) == 4,
         "Expected read of 6 to return 4 because we are EOF");
  ASSERT(memcmp(buf2, "klmn", 4) == 0, "Expected to get klmn back");

  bufread_destroy(br);
  close(memfd);
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

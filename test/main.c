#include <locale.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include "test.h"

void handle_abort(int sig) {
  (void)sig;
  exit(1);
}

int main(void) {
  // Use a hardcoded locale to get a
  // predictable env.
  setlocale(LC_ALL, "en_US.UTF-8");
  signal(SIGABRT, handle_abort);

  struct timespec test_begin;
  clock_gettime(CLOCK_MONOTONIC, &test_begin);

  printf("\n🌍 \x1b[1;36mRunning utf8 tests...\x1b[0m\n");
  run_utf8_tests();

  printf("\n📜 \x1b[1;36mRunning text tests...\x1b[0m\n");
  run_text_tests();

  printf("\n⏪ \x1b[1;36mRunning undo tests...\x1b[0m\n");
  run_undo_tests();

  printf("\n🕴️ \x1b[1;36mRunning buffer tests...\x1b[0m\n");
  run_buffer_tests();

  printf("\n🐒 \x1b[1;36mRunning command tests...\x1b[0m\n");
  run_command_tests();

  printf("\n📠 \x1b[1;36mRunning keyboard tests...\x1b[0m\n");
  run_keyboard_tests();

  printf("\n💾 \x1b[1;36mRunning allocator tests...\x1b[0m\n");
  run_allocator_tests();

  printf("\n🐜 \x1b[1;36mRunning minibuffer tests...\x1b[0m\n");
  run_minibuffer_tests();

  printf("\n 📓 \x1b[1;36mRunning settings tests...\x1b[0m\n");
  run_settings_tests();

  printf("\n 🎁 \x1b[1;36mRunning container tests...\x1b[0m\n");
  run_container_tests();

#if defined(LSP_ENABLED)
  printf("\n 📃 \x1b[1;36mRunning JSON tests...\x1b[0m\n");
  run_json_tests();
#endif

  struct timespec elapsed;
  clock_gettime(CLOCK_MONOTONIC, &elapsed);
  uint64_t elapsed_nanos =
      ((uint64_t)elapsed.tv_sec * 1e9 + (uint64_t)elapsed.tv_nsec) -
      ((uint64_t)test_begin.tv_sec * 1e9 + (uint64_t)test_begin.tv_nsec);
  printf("\n🎉 \x1b[1;32mDone! All tests successful in %.2f ms!\x1b[0m\n",
         (double)elapsed_nanos / 1e6);

  return 0;
}

#include <locale.h>
#include <signal.h>
#include <stdlib.h>

#include "test.h"

void handle_abort() { exit(1); }

int main() {
  setlocale(LC_ALL, "");
  signal(SIGABRT, handle_abort);

  printf("\n🌍 \x1b[1;36mRunning utf8 tests...\x1b[0m\n");
  run_utf8_tests();

  printf("\n📜 \x1b[1;36mRunning text tests...\x1b[0m\n");
  run_text_tests();

  printf("\n🕴️ \x1b[1;36mRunning buffer tests...\x1b[0m\n");
  run_buffer_tests();

  printf("\n🐒 \x1b[1;36mRunning command tests...\x1b[0m\n");
  run_command_tests();

  printf("\n🎉 \x1b[1;32mDone! All tests successful!\x1b[0m\n");
  return 0;
}

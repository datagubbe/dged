#ifndef _TEST_H_
#define _TEST_H_

#include <stdio.h>

#define run_test(fn)                                                           \
  printf("    🧜 running \x1b[1;36m" #fn "\033[0m... ");                     \
  fflush(stdout);                                                              \
  fn();                                                                        \
  printf("\033[32mok!\033[0m\n");

void run_buffer_tests(void);
void run_utf8_tests(void);
void run_text_tests(void);
void run_undo_tests(void);
void run_command_tests(void);
void run_keyboard_tests(void);
void run_allocator_tests(void);
void run_minibuffer_tests(void);
void run_settings_tests(void);
void run_container_tests(void);
void run_json_tests(void);

#endif

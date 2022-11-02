#include <stdio.h>

#define run_test(fn)                                                           \
  printf("    🧜 running \x1b[1;36m" #fn "\033[0m... ");                     \
  fflush(stdout);                                                              \
  fn();                                                                        \
  printf("\033[32mok!\033[0m\n");

void run_buffer_tests();
void run_utf8_tests();
void run_text_tests();

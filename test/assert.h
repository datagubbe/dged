#include <stdbool.h>

#define ASSERT(cond, msg) assert(cond, #cond, __FILE__, __LINE__, msg)
#define ASSERT_STR_EQ(left, right, msg)                                        \
  assert_streq(left, right, __FILE__, __LINE__, msg)

void assert(bool cond, const char *cond_str, const char *file, int line,
            const char *msg);
void assert_streq(const char *left, const char *right, const char *file,
                  int line, const char *msg);

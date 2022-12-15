#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

struct buffer;
void minibuffer_init(struct buffer *buffer);

void minibuffer_echo(const char *fmt, ...);
void minibuffer_echo_timeout(uint32_t timeout, const char *fmt, ...);
void minibuffer_clear();
bool minibuffer_displaying();

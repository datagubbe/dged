#ifndef _SYNTAX_H
#define _SYNTAX_H

#include <stdint.h>

void syntax_init(uint32_t grammar_path_len, const char *grammar_path[]);
void syntax_teardown();

#endif

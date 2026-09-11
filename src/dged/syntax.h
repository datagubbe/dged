#ifndef _SYNTAX_H
#define _SYNTAX_H

#include <stdint.h>

#include "location.h"
#include "s8.h"

struct commands;
struct buffer;

struct syntax_node {
  bool valid;
  union {
    struct s8 error;
    struct s8 type;
  };
  struct s8 grammar_type;
  struct s8 expr;
};

void syntax_init(uint32_t grammar_path_len, const char *grammar_path[]);
struct syntax_node syntax_node_at(struct buffer *buffer, struct location at);
void syntax_node_free(struct syntax_node *node);
void syntax_teardown(void);

#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vec.h"

enum token_type {
  Token_Comment,
  Token_Key,
  Token_StringValue,
  Token_BoolValue,
  Token_IntValue,
  Token_Table,
  Token_InlineTable,

  Token_Error,
};

struct token {
  enum token_type type;
  void *data;
  uint32_t len;
  uint32_t row;
  uint32_t col;
};

typedef size_t (*getbytes)(size_t nbytes, uint8_t *buf, void *userdata);
struct reader {
  getbytes getbytes;
  void *userdata;
};

struct parser {
  uint32_t row;
  uint32_t col;

  struct reader reader;
  VEC(uint8_t) buffer;
};

struct parser parser_create(struct reader reader);
void parser_destroy(struct parser *parser);

bool parser_next_token(struct parser *state, struct token *token_out);

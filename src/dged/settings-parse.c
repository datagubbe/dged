#include "settings-parse.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utf8.h"

enum byte_class {
  Byte_Alphanumeric,
  Byte_Symbol,
};

struct parser parser_create(struct reader reader) {
  struct parser state = {
      .row = 0,
      .col = 0,
      .reader = reader,
  };

  VEC_INIT(&state.buffer, 32);

  return state;
}

void parser_destroy(struct parser *parser) { VEC_DESTROY(&parser->buffer); }

static enum byte_class classify(uint8_t byte) {
  if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
      (byte >= '0' && byte <= '9') || byte == '_' || byte == '-' ||
      utf8_byte_is_unicode(byte)) {
    return Byte_Alphanumeric;
  }

  return Byte_Symbol;
}

static void trim_parse_buffer_whitespace(void **data, uint32_t *len) {
  uint8_t *d = (uint8_t *)*data;
  uint32_t new_len = *len;

  // beginning
  while (d[0] == ' ' || d[0] == '\t') {
    --new_len;
    ++d;
  }

  // end
  while (d[new_len - 1] == ' ' || d[new_len - 1] == '\t') {
    --new_len;
  }

  *data = d;
  *len = new_len;
}

static bool read_data_with_initial(struct parser *state, uint8_t *initial_byte,
                                   uint8_t end, void **data_out,
                                   uint32_t *len_out) {
  uint8_t byte;
  VEC_CLEAR(&state->buffer);
  if (initial_byte != NULL) {
    VEC_PUSH(&state->buffer, *initial_byte);
  }

  while (state->reader.getbytes(1, &byte, state->reader.userdata) > 0 &&
         byte != end) {
    ++state->col;
    VEC_PUSH(&state->buffer, byte);
  }

  *data_out = VEC_FRONT(&state->buffer);
  *len_out = VEC_SIZE(&state->buffer);

  trim_parse_buffer_whitespace(data_out, len_out);

  return byte == end;
}

static bool read_data(struct parser *state, uint8_t end, void **data_out,
                      uint32_t *len_out) {
  return read_data_with_initial(state, NULL, end, data_out, len_out);
}

static bool discard(struct parser *state, uint8_t end) {
  uint8_t byte;
  while (state->reader.getbytes(1, &byte, state->reader.userdata) > 0 &&
         byte != end) {
    ++state->col;
  }

  return byte == end;
}

static void errtoken(struct token *token_out, const char *fmt, ...) {
  static char errmsgbuf[256] = {0};
  va_list args;
  va_start(args, fmt);
  size_t written = vsnprintf(errmsgbuf, 256, fmt, args);
  va_end(args);

  token_out->type = Token_Error;
  token_out->data = errmsgbuf;
  token_out->len = written;
}

bool parser_next_token(struct parser *state, struct token *token_out) {
  uint8_t byte;
  static bool parse_value = false;
  static int64_t int_value = 0;
  static bool bool_value = false;

  memset(token_out, 0, sizeof(struct token));

  while (state->reader.getbytes(1, &byte, state->reader.userdata) > 0) {
    bool multiline = false;
    switch (classify(byte)) {
    case Byte_Alphanumeric: // unquoted key / value
      if (!parse_value) {
        token_out->type = Token_Key;
        token_out->row = state->row;
        token_out->col = state->col;

        if (!read_data_with_initial(state, &byte, '=', &token_out->data,
                                    &token_out->len)) {
          errtoken(token_out, "Unexpected EOF while looking for end of key");
          return true;
        }

        parse_value = true;
      } else {
        parse_value = false;
        token_out->row = state->row;
        token_out->col = state->col;

        if (byte >= '0' && byte <= '9') {
          token_out->type = Token_IntValue;
          void *data;
          uint32_t len;
          read_data_with_initial(state, &byte, '\n', &data, &len);

          char *s = calloc(len + 1, 1);
          strncpy(s, (char *)data, len);

          errno = 0;
          int_value = strtol(s, NULL, 0);
          free(s);
          if (errno != 0) {
            errtoken(token_out, "Invalid integer value %.*s: %s", len,
                     (char *)data, strerror(errno));
            return true;
          }

          token_out->data = &int_value;
          token_out->len = 0;
        } else if (byte == 't' || byte == 'f') {
          token_out->type = Token_BoolValue;
          void *data = NULL;
          uint32_t len = 0;
          read_data_with_initial(state, &byte, '\n', &data, &len);

          if (strncmp((char *)data, "true", len) == 0) {
            bool_value = true;
            token_out->data = &bool_value;
            token_out->len = 0;
          } else if (strncmp((char *)data, "false", len) == 0) {
            bool_value = false;
            token_out->data = &bool_value;
            token_out->len = 0;
          } else {
            errtoken(token_out, "Invalid bool value: %.*s", len, (char *)data);
          }
        }
      }

      return true;

    case Byte_Symbol:
      switch (byte) {
      case '#': // comment
        token_out->type = Token_Comment;
        token_out->row = state->row;
        token_out->col = state->col;
        if (!read_data(state, '\n', &token_out->data, &token_out->len)) {
          errtoken(token_out,
                   "Unexpected EOF while looking for end of comment line");
          return true;
        }

        uint8_t *data = (uint8_t *)token_out->data;
        if (data[token_out->len - 1] == '\r') {
          --token_out->len;
        }

        state->col = 0;
        ++state->row;

        return true;

      case '{': // inline table
        parse_value = false;
        token_out->type = Token_InlineTable;
        token_out->row = state->row;
        token_out->col = state->col;
        return true;
        break;

      case '}': // end inline table
        parse_value = false;
        break;

      case '[': // table open
        token_out->type = Token_Table;
        token_out->row = state->row;
        token_out->col = state->col;
        if (!read_data(state, ']', &token_out->data, &token_out->len)) {
          errtoken(token_out, "Unexpected EOF while looking for matching ']'");
          return true;
        }

        ++state->col;
        return true;

      case '"': // quoted key or string value
        multiline = false;
        if (parse_value) {
          token_out->type = Token_StringValue;
        } else {
          token_out->type = Token_Key;
        }
        token_out->row = state->row;
        token_out->col = state->col;

        // check for multiline
        uint32_t numquotes = 1;
        while (state->reader.getbytes(1, &byte, state->reader.userdata) > 0 &&
               byte == '"') {
          ++numquotes;
        }

        if (numquotes == 3) {
          multiline = true;
        }

        if (!read_data_with_initial(state, &byte, '"', &token_out->data,
                                    &token_out->len)) {
          errtoken(token_out, "Unexpected EOF while looking for matching '\"'");
          parse_value = false;
          return true;
        }

        if (!parse_value) {
          discard(state, '=');
        }

        if (multiline) {
          discard(state, '"');
          discard(state, '"');
        }

        ++state->col;
        parse_value = false;
        return true;

      case '\n':
      case '\r':
        state->col = 0;
        ++state->row;
        break;
      }
      break;
    }
  }

  return false;
}

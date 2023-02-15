#include "text.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "display.h"
#include "signal.h"
#include "utf8.h"

enum flags {
  LineChanged = 1 << 0,
};

struct line {
  uint8_t *data;
  uint8_t flags;
  uint32_t nbytes;
  uint32_t nchars;
};

struct text {
  // raw bytes without any null terminators
  struct line *lines;
  uint32_t nlines;
  uint32_t capacity;
};

struct text *text_create(uint32_t initial_capacity) {
  struct text *txt = calloc(1, sizeof(struct text));
  txt->lines = calloc(initial_capacity, sizeof(struct line));
  txt->capacity = initial_capacity;
  txt->nlines = 0;

  return txt;
}

void text_destroy(struct text *text) {
  for (uint32_t li = 0; li < text->nlines; ++li) {
    free(text->lines[li].data);
    text->lines[li].data = NULL;
    text->lines[li].flags = 0;
    text->lines[li].nbytes = 0;
    text->lines[li].nchars = 0;
  }

  free(text->lines);

  free(text);
}

void text_clear(struct text *text) {
  for (uint32_t li = 0; li < text->nlines; ++li) {
    free(text->lines[li].data);
    text->lines[li].data = NULL;
    text->lines[li].flags = 0;
    text->lines[li].nbytes = 0;
    text->lines[li].nchars = 0;
  }

  text->nlines = 0;
}

// given `char_idx` as a character index, return the byte index
uint32_t charidx_to_byteidx(struct line *line, uint32_t char_idx) {
  if (char_idx > line->nchars) {
    return line->nbytes;
  }
  return utf8_nbytes(line->data, line->nbytes, char_idx);
}

uint32_t text_col_to_byteindex(struct text *text, uint32_t line, uint32_t col) {
  return charidx_to_byteidx(&text->lines[line], col);
}

// given `byte_idx` as a byte index, return the character index
uint32_t byteidx_to_charidx(struct line *line, uint32_t byte_idx) {
  if (byte_idx > line->nbytes) {
    return line->nchars;
  }

  return utf8_nchars(line->data, byte_idx);
}

uint32_t text_byteindex_to_col(struct text *text, uint32_t line,
                               uint32_t byteindex) {
  return byteidx_to_charidx(&text->lines[line], byteindex);
}

void append_empty_lines(struct text *text, uint32_t numlines) {

  if (text->nlines + numlines >= text->capacity) {
    text->capacity += text->capacity + numlines > text->capacity * 2
                          ? numlines + 1
                          : text->capacity;
    text->lines = realloc(text->lines, sizeof(struct line) * text->capacity);
  }

  for (uint32_t i = 0; i < numlines; ++i) {
    struct line *nline = &text->lines[text->nlines];
    nline->data = NULL;
    nline->nbytes = 0;
    nline->nchars = 0;
    nline->flags = 0;

    ++text->nlines;
  }

  if (text->nlines > text->capacity) {
    printf("text->nlines: %d, text->capacity: %d\n", text->nlines,
           text->capacity);
    raise(SIGTRAP);
  }
}

void ensure_line(struct text *text, uint32_t line) {
  if (line >= text->nlines) {
    append_empty_lines(text, line - text->nlines + 1);
  }
}

// It is assumed that `data` does not contain any \n, that is handled by
// higher-level functions
void insert_at(struct text *text, uint32_t line, uint32_t col, uint8_t *data,
               uint32_t len, uint32_t nchars) {

  if (len == 0) {
    return;
  }

  ensure_line(text, line);

  struct line *l = &text->lines[line];

  l->nbytes += len;
  l->nchars += nchars;
  l->flags = LineChanged;
  l->data = realloc(l->data, l->nbytes);

  uint32_t bytei = charidx_to_byteidx(l, col);

  // move following bytes out of the way
  if (bytei + len < l->nbytes) {
    uint32_t start = bytei + len;
    memmove(l->data + start, l->data + bytei, l->nbytes - start);
  }

  // insert new chars
  memcpy(l->data + bytei, data, len);
}

uint32_t text_line_length(struct text *text, uint32_t lineidx) {
  return text->lines[lineidx].nchars;
}

uint32_t text_line_size(struct text *text, uint32_t lineidx) {
  return text->lines[lineidx].nbytes;
}

uint32_t text_num_lines(struct text *text) { return text->nlines; }

void split_line(uint32_t col, struct line *line, struct line *next) {
  uint8_t *data = line->data;
  uint32_t nbytes = line->nbytes;
  uint32_t nchars = line->nchars;

  uint32_t chari = col;
  uint32_t bytei = charidx_to_byteidx(line, chari);

  line->nbytes = bytei;
  line->nchars = chari;
  next->nbytes = nbytes - bytei;
  next->nchars = nchars - chari;
  line->flags = next->flags = line->flags;

  next->data = NULL;
  line->data = NULL;

  // first, handle some cases where the new line or the pre-existing one is
  // empty
  if (next->nbytes == 0) {
    line->data = data;
  } else if (line->nbytes == 0) {
    next->data = data;
  } else {
    // actually split the line
    next->data = (uint8_t *)realloc(next->data, next->nbytes);
    memcpy(next->data, data + bytei, next->nbytes);

    line->data = (uint8_t *)realloc(line->data, line->nbytes);
    memcpy(line->data, data, line->nbytes);

    free(data);
  }
}

void mark_lines_changed(struct text *text, uint32_t line, uint32_t nlines) {
  for (uint32_t linei = line; linei < (line + nlines); ++linei) {
    text->lines[linei].flags |= LineChanged;
  }
}

void shift_lines(struct text *text, uint32_t start, int32_t direction) {
  struct line *dest = &text->lines[((int64_t)start + direction)];
  struct line *src = &text->lines[start];
  uint32_t nlines = text->nlines - (dest > src ? (start + direction) : start);
  memmove(dest, src, nlines * sizeof(struct line));
}

void new_line_at(struct text *text, uint32_t line, uint32_t col) {
  ensure_line(text, line);

  uint32_t newline = line + 1;
  bool has_newline = col < text->lines[line].nchars || newline < text->nlines;
  append_empty_lines(text, has_newline ? 1 : 0);

  mark_lines_changed(text, line, text->nlines - line);

  // move following lines out of the way, if there are any
  if (newline + 1 < text->nlines) {
    shift_lines(text, newline, 1);
  }

  // split line if needed
  split_line(col, &text->lines[line], &text->lines[newline]);
}

void delete_line(struct text *text, uint32_t line) {
  if (text->nlines == 0) {
    return;
  }

  mark_lines_changed(text, line, text->nlines - line);

  free(text->lines[line].data);
  text->lines[line].data = NULL;

  if (line + 1 < text->nlines) {
    shift_lines(text, line + 1, -1);
  }

  --text->nlines;
  text->lines[text->nlines].data = NULL;
  text->lines[text->nlines].nbytes = 0;
  text->lines[text->nlines].nchars = 0;
}

void text_append(struct text *text, uint8_t *bytes, uint32_t nbytes,
                 uint32_t *lines_added, uint32_t *cols_added) {
  uint32_t line = text->nlines > 0 ? text->nlines - 1 : 0;
  uint32_t col = text_line_length(text, line);

  text_insert_at(text, line, col, bytes, nbytes, lines_added, cols_added);
}

void text_insert_at(struct text *text, uint32_t line, uint32_t col,
                    uint8_t *bytes, uint32_t nbytes, uint32_t *lines_added,
                    uint32_t *cols_added) {
  uint32_t linelen = 0, start_line = line;

  *cols_added = 0;
  for (uint32_t bytei = 0; bytei < nbytes; ++bytei) {
    uint8_t byte = bytes[bytei];
    if (byte == '\n') {
      uint8_t *line_data = bytes + (bytei - linelen);
      uint32_t nchars = utf8_nchars(line_data, linelen);

      insert_at(text, line, col, line_data, linelen, nchars);

      col += nchars;
      if (linelen == 0 || col < text_line_length(text, line)) {
        new_line_at(text, line, col);
      }

      ++line;
      linelen = 0;
      col = 0;
    } else {
      ++linelen;
    }
  }

  // handle remaining
  if (linelen > 0) {
    uint8_t *line_data = bytes + (nbytes - linelen);
    uint32_t nchars = utf8_nchars(line_data, linelen);
    insert_at(text, line, col, line_data, linelen, nchars);
    *cols_added = nchars;
  }

  *lines_added = line - start_line;
}

void text_delete(struct text *text, uint32_t start_line, uint32_t start_col,
                 uint32_t end_line, uint32_t end_col) {

  uint32_t maxline = text->nlines > 0 ? text->nlines - 1 : 0;

  // make sure we stay inside
  if (start_line > maxline) {
    start_line = maxline;
    start_col = text->lines[start_line].nchars > 0
                    ? text->lines[start_line].nchars - 1
                    : 0;
  }

  if (end_line > maxline) {
    end_line = maxline;
    end_col = text->lines[end_line].nchars;
  }

  struct line *firstline = &text->lines[start_line];
  struct line *lastline = &text->lines[end_line];

  // clamp column
  if (start_col > firstline->nchars) {
    start_col = firstline->nchars > 0 ? firstline->nchars - 1 : 0;
  }

  // handle deletion of newlines
  if (end_col > lastline->nchars) {
    if (end_line + 1 < text->nlines) {
      end_col = 0;
      ++end_line;
      lastline = &text->lines[end_line];
    } else {
      end_col = lastline->nchars;
    }
  }

  uint32_t bytei = utf8_nbytes(lastline->data, lastline->nbytes, end_col);
  if (lastline == firstline) {
    // in this case we can "overwrite"
    uint32_t dstbytei =
        utf8_nbytes(firstline->data, firstline->nbytes, start_col);
    memcpy(firstline->data + dstbytei, lastline->data + bytei,
           lastline->nbytes - bytei);
  } else {
    // otherwise we actually have to copy from the last line
    insert_at(text, start_line, start_col, lastline->data + bytei,
              lastline->nbytes - bytei, lastline->nchars - end_col);
  }

  firstline->nchars = start_col + (lastline->nchars - end_col);
  firstline->nbytes =
      utf8_nbytes(firstline->data, firstline->nbytes, start_col) +
      (lastline->nbytes - bytei);

  // delete full lines, backwards to not shift old, crappy data upwards
  for (uint32_t linei = end_line >= text->nlines ? end_line - 1 : end_line;
       linei > start_line; --linei) {
    delete_line(text, linei);
  }

  // if this is the last line in the buffer, and it turns out empty, remove it
  if (firstline->nbytes == 0 && start_line == text->nlines - 1) {
    delete_line(text, start_line);
  }
}

void text_for_each_chunk(struct text *text, chunk_cb callback, void *userdata) {
  // if representation of text is changed, this can be changed as well
  text_for_each_line(text, 0, text->nlines, callback, userdata);
}

void text_for_each_line(struct text *text, uint32_t line, uint32_t nlines,
                        chunk_cb callback, void *userdata) {
  uint32_t nlines_max =
      (line + nlines) > text->nlines ? text->nlines : (line + nlines);
  for (uint32_t li = line; li < nlines_max; ++li) {
    struct line *src_line = &text->lines[li];
    struct text_chunk line = (struct text_chunk){
        .text = src_line->data,
        .nbytes = src_line->nbytes,
        .nchars = src_line->nchars,
        .line = li,
    };
    callback(&line, userdata);
  }
}

struct text_chunk text_get_line(struct text *text, uint32_t line) {
  struct line *src_line = &text->lines[line];
  return (struct text_chunk){
      .text = src_line->data,
      .nbytes = src_line->nbytes,
      .nchars = src_line->nchars,
      .line = line,
  };
}

struct copy_cmd {
  uint32_t line;
  uint32_t byteoffset;
  uint32_t nbytes;
};

struct text_chunk text_get_region(struct text *text, uint32_t start_line,
                                  uint32_t start_col, uint32_t end_line,
                                  uint32_t end_col) {
  if (start_line == end_line && start_col == end_col) {
    return (struct text_chunk){0};
  }

  struct line *first_line = &text->lines[start_line];
  struct line *last_line = &text->lines[end_line];

  if (start_col > first_line->nchars) {
    return (struct text_chunk){0};
  }

  // handle copying of newlines
  if (end_col > last_line->nchars) {
    ++end_line;
    end_col = 0;
    last_line = &text->lines[end_line];
  }

  uint32_t nlines = end_line - start_line + 1;
  struct copy_cmd *copy_cmds = calloc(nlines, sizeof(struct copy_cmd));

  uint32_t total_chars = 0, total_bytes = 0;
  for (uint32_t line = start_line; line <= end_line; ++line) {
    struct line *l = &text->lines[line];
    total_chars += l->nchars;
    total_bytes += l->nbytes;

    struct copy_cmd *cmd = &copy_cmds[line - start_line];
    cmd->line = line;
    cmd->byteoffset = 0;
    cmd->nbytes = l->nbytes;
  }

  // correct first line
  struct copy_cmd *cmd_first = &copy_cmds[0];
  uint32_t byteoff =
      utf8_nbytes(first_line->data, first_line->nbytes, start_col);
  cmd_first->byteoffset += byteoff;
  cmd_first->nbytes -= byteoff;
  total_bytes -= byteoff;
  total_chars -= start_col;

  // correct last line
  struct copy_cmd *cmd_last = &copy_cmds[nlines - 1];
  uint32_t byteindex = utf8_nbytes(last_line->data, last_line->nbytes, end_col);
  cmd_last->nbytes -= (last_line->nchars - end_col);
  total_bytes -= (last_line->nbytes - byteindex);
  total_chars -= (last_line->nchars - end_col);

  uint8_t *data = (uint8_t *)malloc(
      total_bytes + /* nr of newline chars */ (end_line - start_line));

  // copy data
  for (uint32_t cmdi = 0, curr = 0; cmdi < nlines; ++cmdi) {
    struct copy_cmd *c = &copy_cmds[cmdi];
    struct line *l = &text->lines[c->line];
    memcpy(data + curr, l->data + c->byteoffset, c->nbytes);
    curr += c->nbytes;

    if (cmdi != (nlines - 1)) {
      data[curr] = '\n';
      ++curr;
      ++total_bytes;
      ++total_chars;
    }
  }

  free(copy_cmds);
  return (struct text_chunk){
      .text = data,
      .line = 0,
      .nbytes = total_bytes,
      .nchars = total_chars,
      .allocated = true,
  };
}

bool text_line_contains_unicode(struct text *text, uint32_t line) {
  return text->lines[line].nbytes != text->lines[line].nchars;
}

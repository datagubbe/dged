#include "text.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "display.h"
#include "signal.h"
#include "utf8.h"
#include "vec.h"

enum flags {
  LineChanged = 1 << 0,
};

struct line {
  uint8_t *data;
  uint8_t flags;
  uint32_t nbytes;
};

struct text_property_entry {
  struct location start;
  struct location end;
  struct text_property property;
};

struct text {
  // raw bytes without any null terminators
  struct line *lines;
  uint32_t nlines;
  uint32_t capacity;
  VEC(struct text_property_entry) properties;
};

struct text *text_create(uint32_t initial_capacity) {
  struct text *txt = calloc(1, sizeof(struct text));
  txt->lines = calloc(initial_capacity, sizeof(struct line));
  txt->capacity = initial_capacity;
  txt->nlines = 0;

  VEC_INIT(&txt->properties, 32);

  return txt;
}

void text_destroy(struct text *text) {
  VEC_DESTROY(&text->properties);

  for (uint32_t li = 0; li < text->nlines; ++li) {
    free(text->lines[li].data);
    text->lines[li].data = NULL;
    text->lines[li].flags = 0;
    text->lines[li].nbytes = 0;
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
  }

  text->nlines = 0;
  text_clear_properties(text);
}

struct utf8_codepoint_iterator
text_line_codepoint_iterator(const struct text *text, uint32_t lineidx) {
  if (lineidx >= text_num_lines(text)) {
    return create_utf8_codepoint_iterator(NULL, 0, 0);
  }

  return create_utf8_codepoint_iterator(text->lines[lineidx].data,
                                        text->lines[lineidx].nbytes, 0);
}

struct utf8_codepoint_iterator
text_chunk_codepoint_iterator(const struct text_chunk *chunk) {
  return create_utf8_codepoint_iterator(chunk->text, chunk->nbytes, 0);
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
    nline->flags = 0;

    ++text->nlines;
  }
}

void ensure_line(struct text *text, uint32_t line) {
  if (line >= text->nlines) {
    append_empty_lines(text, line - text->nlines + 1);
  }
}

// It is assumed that `data` does not contain any \n, that is handled by
// higher-level functions
static void insert_at(struct text *text, uint32_t line, uint32_t offset,
                      uint8_t *data, uint32_t len) {

  if (len == 0) {
    return;
  }

  ensure_line(text, line);

  struct line *l = &text->lines[line];

  l->nbytes += len;
  l->flags = LineChanged;
  l->data = realloc(l->data, l->nbytes);

  uint32_t bytei = offset;

  // move following bytes out of the way
  if (bytei + len < l->nbytes) {
    uint32_t start = bytei + len;
    memmove(l->data + start, l->data + bytei, l->nbytes - start);
  }

  // insert new chars
  memcpy(l->data + bytei, data, len);
}

uint32_t text_line_size(const struct text *text, uint32_t lineidx) {
  if (lineidx >= text_num_lines(text)) {
    return 0;
  }

  return text->lines[lineidx].nbytes;
}

uint32_t text_num_lines(const struct text *text) { return text->nlines; }

static void split_line(struct text *text, uint32_t offset, uint32_t lineidx,
                       uint32_t newlineidx) {
  struct line *line = &text->lines[lineidx];
  struct line *next = &text->lines[newlineidx];

  uint8_t *data = line->data;
  uint32_t nbytes = line->nbytes;
  uint32_t bytei = offset;

  line->nbytes = bytei;
  next->nbytes = nbytes - bytei;
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

void new_line_at(struct text *text, uint32_t line, uint32_t offset) {
  ensure_line(text, line);

  uint32_t newline = line + 1;
  append_empty_lines(text, 1);

  mark_lines_changed(text, line, text->nlines - line);

  // move following lines out of the way, if there are any
  if (newline + 1 < text->nlines) {
    shift_lines(text, newline, 1);
  }

  // split line if needed
  split_line(text, offset, line, newline);
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
}

static void text_insert_at_inner(struct text *text, uint32_t line,
                                 uint32_t offset, uint8_t *bytes,
                                 uint32_t nbytes, uint32_t *lines_added) {
  uint32_t linelen = 0, start_line = line;

  for (uint32_t bytei = 0; bytei < nbytes; ++bytei) {
    uint8_t byte = bytes[bytei];
    if (byte == '\n') {
      uint8_t *line_data = bytes + (bytei - linelen);
      insert_at(text, line, offset, line_data, linelen);

      offset += linelen;
      new_line_at(text, line, offset);

      ++line;
      linelen = 0;
      offset = 0;
    } else {
      ++linelen;
    }
  }

  // handle remaining
  if (linelen > 0) {
    uint8_t *line_data = bytes + (nbytes - linelen);
    insert_at(text, line, offset, line_data, linelen);
  }

  *lines_added = line - start_line;
}

void text_append(struct text *text, uint8_t *bytes, uint32_t nbytes,
                 uint32_t *lines_added) {
  uint32_t line = text->nlines > 0 ? text->nlines - 1 : 0;
  uint32_t offset = text_line_size(text, line);
  text_insert_at_inner(text, line, offset, bytes, nbytes, lines_added);
}

void text_insert_at(struct text *text, uint32_t line, uint32_t offset,
                    uint8_t *bytes, uint32_t nbytes, uint32_t *lines_added) {
  text_insert_at_inner(text, line, offset, bytes, nbytes, lines_added);
}

void text_delete(struct text *text, uint32_t start_line, uint32_t start_offset,
                 uint32_t end_line, uint32_t end_offset) {

  if (text->nlines == 0) {
    return;
  }

  uint32_t maxline = text->nlines > 0 ? text->nlines - 1 : 0;

  if (start_line > maxline) {
    return;
  }

  if (end_line > maxline) {
    end_line = maxline;
    end_offset = text_line_size(text, end_line);
  }

  struct line *firstline = &text->lines[start_line];
  struct line *lastline = &text->lines[end_line];

  // clamp column
  uint32_t firstline_len = text_line_size(text, start_line);
  if (start_offset > firstline_len) {
    start_offset = firstline_len > 0 ? firstline_len - 1 : 0;
  }

  // handle deletion of newlines
  uint32_t lastline_len = text_line_size(text, end_line);
  if (end_offset > lastline_len) {
    if (end_line + 1 < text->nlines) {
      end_offset = 0;
      ++end_line;
      lastline = &text->lines[end_line];
    } else {
      end_offset = lastline_len;
    }
  }

  uint32_t srcbytei = end_offset;
  uint32_t dstbytei = start_offset;
  uint32_t ncopy = lastline->nbytes - srcbytei;
  if (lastline == firstline) {
    // in this case we can "overwrite"
    memmove(firstline->data + dstbytei, lastline->data + srcbytei, ncopy);
  } else {
    // otherwise we actually have to copy from the last line
    insert_at(text, start_line, start_offset, lastline->data + srcbytei, ncopy);
  }

  // new byte count is whatever we had before (left of dstbytei)
  // plus what we copied
  firstline->nbytes = dstbytei + ncopy;

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
        .allocated = false,
        .text = src_line->data,
        .nbytes = src_line->nbytes,
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
      .line = line,
      .allocated = false,
  };
}

struct copy_cmd {
  uint32_t line;
  uint32_t byteoffset;
  uint32_t nbytes;
};

struct text_chunk text_get_region(struct text *text, uint32_t start_line,
                                  uint32_t start_offset, uint32_t end_line,
                                  uint32_t end_offset) {
  if (start_line == end_line && start_offset == end_offset) {
    return (struct text_chunk){0};
  }

  struct line *first_line = &text->lines[start_line];
  struct line *last_line = &text->lines[end_line];
  uint32_t first_line_len = first_line->nbytes;
  uint32_t last_line_len = last_line->nbytes;

  if (start_offset > first_line_len) {
    return (struct text_chunk){0};
  }

  // handle copying of newlines
  if (end_offset > last_line_len) {
    ++end_line;
    end_offset = 0;
    last_line = &text->lines[end_line];
  }

  uint32_t nlines = end_line - start_line + 1;
  struct copy_cmd *copy_cmds = calloc(nlines, sizeof(struct copy_cmd));

  uint32_t total_bytes = 0;
  for (uint32_t line = start_line; line <= end_line; ++line) {
    struct line *l = &text->lines[line];
    total_bytes += l->nbytes;

    struct copy_cmd *cmd = &copy_cmds[line - start_line];
    cmd->line = line;
    cmd->byteoffset = 0;
    cmd->nbytes = l->nbytes;
  }

  // correct first line
  struct copy_cmd *cmd_first = &copy_cmds[0];
  cmd_first->byteoffset += start_offset;
  cmd_first->nbytes -= start_offset;
  total_bytes -= start_offset;

  // correct last line
  struct copy_cmd *cmd_last = &copy_cmds[nlines - 1];
  cmd_last->nbytes -= (last_line->nbytes - end_offset);
  total_bytes -= (last_line->nbytes - end_offset);

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
    }
  }

  free(copy_cmds);
  return (struct text_chunk){
      .text = data,
      .line = 0,
      .nbytes = total_bytes,
      .allocated = true,
  };
}

void text_add_property(struct text *text, uint32_t start_line,
                       uint32_t start_offset, uint32_t end_line,
                       uint32_t end_offset, struct text_property property) {
  struct text_property_entry entry = {
      .start = (struct location){.line = start_line, .col = start_offset},
      .end = (struct location){.line = end_line, .col = end_offset},
      .property = property,
  };
  VEC_PUSH(&text->properties, entry);
}

void text_get_properties(struct text *text, uint32_t line, uint32_t offset,
                         struct text_property **properties,
                         uint32_t max_nproperties, uint32_t *nproperties) {
  struct location location = {.line = line, .col = offset};
  uint32_t nres = 0;
  VEC_FOR_EACH(&text->properties, struct text_property_entry * prop) {
    if (location_is_between(location, prop->start, prop->end)) {
      properties[nres] = &prop->property;
      ++nres;

      if (nres == max_nproperties) {
        break;
      }
    }
  }
  *nproperties = nres;
}

void text_clear_properties(struct text *text) { VEC_CLEAR(&text->properties); }

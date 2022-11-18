#include "text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "display.h"
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

  // we always have one line, since add line adds a second one
  txt->nlines = 1;

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
}

// given `char_idx` as a character index, return the byte index
uint32_t charidx_to_byteidx(struct line *line, uint32_t char_idx) {
  if (char_idx > line->nchars) {
    return line->nbytes;
  }
  return utf8_nbytes(line->data, char_idx);
}

// TODO: grapheme clusters
// given `byte_idx` as a byte index, return the character index
uint32_t byteidx_to_charidx(struct line *line, uint32_t byte_idx) {
  if (byte_idx > line->nbytes) {
    return line->nchars;
  }

  return utf8_nchars(line->data, byte_idx);
}

uint32_t char_byte_size(struct line *line, uint32_t byte_idx) {
  return utf8_nbytes(line->data + byte_idx, 1);
}

void append_to_line(struct line *line, uint32_t col, uint8_t *text,
                    uint32_t len, uint32_t nchars) {

  if (len == 0) {
    return;
  }

  line->nbytes += len;
  line->nchars += nchars;
  line->flags = LineChanged;
  line->data = realloc(line->data, line->nbytes);

  uint32_t bytei = charidx_to_byteidx(line, col);

  // move chars out of the way
  if (col + nchars < line->nchars) {
    uint32_t nextcbytei = charidx_to_byteidx(line, col + nchars);
    memmove(line->data + nextcbytei, line->data + bytei,
            line->nbytes - nextcbytei);
  }

  // insert new chars
  memcpy(line->data + bytei, text, len);
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

  // first, handle some cases where the new line or the pre-existing one is
  // empty
  if (next->nbytes == 0) {
    line->data = data;
  } else if (line->nbytes == 0) {
    next->data = data;
  } else {
    // actually split the line
    next->data = (uint8_t *)malloc(next->nbytes);
    memcpy(next->data, data + bytei, next->nbytes);

    line->data = (uint8_t *)malloc(line->nbytes);
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
  struct line *dest = text->lines + ((int64_t)start + direction);
  struct line *src = text->lines + start;
  uint32_t nlines = text->nlines - (dest > src ? (start + direction) : start);
  memmove(dest, src, nlines * sizeof(struct line));
}

void new_line_at(struct text *text, uint32_t line, uint32_t col) {
  if (text->nlines == text->capacity) {
    text->capacity *= 2;
    text->lines = realloc(text->lines, sizeof(struct line) * text->capacity);
  }

  struct line *nline = &text->lines[text->nlines];
  nline->data = NULL;
  nline->nbytes = 0;
  nline->nchars = 0;
  nline->flags = 0;

  ++text->nlines;

  mark_lines_changed(text, line, text->nlines - line);

  // move following lines out of the way
  shift_lines(text, line + 1, 1);

  // split line if needed
  struct line *pl = &text->lines[line];
  struct line *cl = &text->lines[line + 1];
  split_line(col, pl, cl);
}

void delete_line(struct text *text, uint32_t line) {
  // always keep a single line
  if (text->nlines == 1) {
    return;
  }

  mark_lines_changed(text, line, text->nlines - line);
  free(text->lines[line].data);
  text->lines[line].data = NULL;

  shift_lines(text, line + 1, -1);

  if (text->nlines > 0) {
    --text->nlines;
    text->lines[text->nlines].data = NULL;
    text->lines[text->nlines].nbytes = 0;
    text->lines[text->nlines].nchars = 0;
  }
}

void text_append(struct text *text, uint32_t line, uint32_t col, uint8_t *bytes,
                 uint32_t nbytes, uint32_t *lines_added, uint32_t *cols_added) {
  uint32_t linelen = 0;
  uint32_t nchars_counted = 0;
  uint32_t nlines_added = 0;
  uint32_t ncols_added = 0;
  for (uint32_t bytei = 0; bytei < nbytes; ++bytei) {
    uint8_t byte = bytes[bytei];
    if (byte == '\n') {
      append_to_line(&text->lines[line], col, bytes + (bytei - linelen),
                     linelen, nchars_counted);

      col += nchars_counted;
      new_line_at(text, line, col);
      ++line;
      ++nlines_added;

      col = text_line_length(text, line);
      linelen = 0;
      nchars_counted = 0;
    } else {
      if (utf8_byte_is_ascii(byte) || utf8_byte_is_unicode_start(byte)) {
        ++nchars_counted;
      }
      ++linelen;
    }
  }

  // handle remaining
  if (linelen > 0) {
    append_to_line(&text->lines[line], col, bytes + (nbytes - linelen), linelen,
                   nchars_counted);
    ncols_added = nchars_counted;
  }

  *lines_added = nlines_added;
  *cols_added = ncols_added;
}

void text_delete(struct text *text, uint32_t line, uint32_t col,
                 uint32_t nchars) {

  // delete chars from current line
  struct line *lp = &text->lines[line];
  uint32_t chars_initial_line =
      col + nchars > lp->nchars ? (lp->nchars - col) : nchars;
  uint32_t bytei = charidx_to_byteidx(lp, col);
  uint32_t nbytes = utf8_nbytes(lp->data + bytei, chars_initial_line);

  memcpy(lp->data + bytei, lp->data + bytei + nbytes,
         lp->nbytes - (bytei + nbytes));

  lp->nbytes -= nbytes;
  lp->nchars -= chars_initial_line;
  lp->flags |= LineChanged;

  uint32_t initial_line = line;
  uint32_t left_to_delete = nchars - chars_initial_line;

  // grab remaining chars from last line to delete from (if any)
  uint32_t src_col = 0;
  while (left_to_delete > 0 && line < text->nlines) {
    ++line;
    --left_to_delete; // newline char

    struct line *lp = &text->lines[line];
    uint32_t deleted_in_line =
        left_to_delete > lp->nchars ? lp->nchars : left_to_delete;
    src_col = deleted_in_line;
    left_to_delete -= deleted_in_line;
  }

  if (line != initial_line) {
    struct line *lp = &text->lines[line];
    uint32_t bytei = charidx_to_byteidx(lp, src_col);
    if (src_col < lp->nchars) {
      append_to_line(&text->lines[initial_line], col, lp->data + bytei,
                     lp->nbytes - bytei, lp->nchars - src_col);
    }
  }

  // delete all lines from current line + 1 to (and including) last line
  for (uint32_t li = initial_line + 1; li <= line; ++li) {
    delete_line(text, li);
  }
}

uint32_t text_render(struct text *text, uint32_t line, uint32_t nlines,
                     struct render_cmd *cmds, uint32_t max_ncmds) {
  uint32_t nlines_max = nlines > text->capacity ? text->capacity : nlines;

  uint32_t ncmds = 0;
  for (uint32_t lineidx = line; lineidx < nlines_max; ++lineidx) {
    struct line *lp = &text->lines[lineidx];
    if (lp->flags & LineChanged) {

      cmds[ncmds] = (struct render_cmd){
          .row = lineidx,
          .col = 0, // TODO: do not redraw full line
          .data = lp->data,
          .len = lp->nbytes,
      };

      lp->flags &= ~(LineChanged);

      ++ncmds;
    }
  }

  return ncmds;
}

void text_for_each_chunk(struct text *text, chunk_cb callback) {
  // if representation of text is changed, this can be changed as well
  text_for_each_line(text, 0, text->nlines, callback);
}

void text_for_each_line(struct text *text, uint32_t line, uint32_t nlines,
                        chunk_cb callback) {
  for (uint32_t li = line; li < (line + nlines); ++li) {
    struct line *src_line = &text->lines[li];
    struct text_chunk line = (struct text_chunk){
        .text = src_line->data,
        .nbytes = src_line->nbytes,
        .nchars = src_line->nchars,
    };
    callback(&line);
  }
}

struct text_chunk text_get_line(struct text *text, uint32_t line) {
  struct line *src_line = &text->lines[line];
  return (struct text_chunk){
      .text = src_line->data,
      .nbytes = src_line->nbytes,
      .nchars = src_line->nchars,
  };
}

bool text_line_contains_unicode(struct text *text, uint32_t line) {
  return text->lines[line].nbytes != text->lines[line].nchars;
}

#include "undo.h"
#include "string.h"

#include <stdio.h>
#include <stdlib.h>

void undo_init(struct undo_stack *undo, uint32_t initial_capacity) {
  undo->top = INVALID_TOP;
  undo->nrecords = 0;
  undo->undo_in_progress = false;

  undo->records = calloc(initial_capacity, sizeof(struct undo_record));
  undo->capacity = initial_capacity;
}

void grow_if_needed(struct undo_stack *undo, uint32_t needed_capacity) {
  if (needed_capacity > undo->capacity) {
    undo->capacity += undo->capacity + needed_capacity > undo->capacity * 2
                          ? needed_capacity
                          : undo->capacity;

    undo->records =
        realloc(undo->records, sizeof(struct undo_record) * undo->capacity);
  }
}

void undo_clear(struct undo_stack *undo) {
  undo->top = INVALID_TOP;
  undo->nrecords = 0;
}

void undo_destroy(struct undo_stack *undo) {
  for (uint32_t i = 0; i < undo->nrecords; ++i) {
    struct undo_record *rec = &undo->records[i];
    if (rec->type == Undo_Delete && rec->delete.data != NULL &&
        rec->delete.nbytes > 0) {
      free(rec->delete.data);
    }
  }
  undo_clear(undo);
  undo->capacity = 0;

  free(undo->records);
  undo->records = NULL;
}

uint32_t undo_push_boundary(struct undo_stack *undo,
                            struct undo_boundary boundary) {
  grow_if_needed(undo, undo->nrecords + 1);

  undo->records[undo->nrecords].type = Undo_Boundary;
  undo->records[undo->nrecords].boundary = boundary;

  if (!undo->undo_in_progress) {
    undo->top = undo->nrecords;
  }

  // we can only have one save point
  if (boundary.save_point) {
    for (uint32_t i = 0; i < undo->nrecords; ++i) {
      if (undo->records[i].type && Undo_Boundary &&
          undo->records[i].boundary.save_point) {
        undo->records[i].boundary.save_point = false;
      }
    }
  }

  ++undo->nrecords;
  return undo->nrecords - 1;
}

bool pos_equal(struct position *a, struct position *b) {
  return a->row == b->row && a->col == b->col;
}

uint32_t undo_push_add(struct undo_stack *undo, struct undo_add add) {
  grow_if_needed(undo, undo->nrecords + 1);

  // "compress"
  if (undo->nrecords > 0 &&
      undo->records[undo->nrecords - 1].type == Undo_Add &&
      pos_equal(&undo->records[undo->nrecords - 1].add.end, &add.begin)) {
    undo->records[undo->nrecords - 1].add.end = add.end;
    return undo->nrecords;
  }

  undo->records[undo->nrecords].type = Undo_Add;
  undo->records[undo->nrecords].add = add;

  if (!undo->undo_in_progress) {
    undo->top = undo->nrecords;
  }

  ++undo->nrecords;
  return undo->nrecords - 1;
}

uint32_t undo_push_delete(struct undo_stack *undo, struct undo_delete delete) {
  grow_if_needed(undo, undo->nrecords + 1);

  undo->records[undo->nrecords].type = Undo_Delete;
  undo->records[undo->nrecords].delete = delete;

  if (!undo->undo_in_progress) {
    undo->top = undo->nrecords;
  }

  ++undo->nrecords;
  return undo->nrecords - 1;
}

void undo_begin(struct undo_stack *undo) { undo->undo_in_progress = true; }

void undo_next(struct undo_stack *undo, struct undo_record **records_out,
               uint32_t *nrecords_out) {
  *nrecords_out = 0;
  *records_out = NULL;

  if (undo->nrecords == 0) {
    return;
  }

  if (undo->top == INVALID_TOP) {
    // reset back to the top (redo)
    undo->top = undo->nrecords - 1;
  }

  uint32_t nrecords = 1;
  struct undo_record *current = &undo->records[undo->top];
  while (undo->top > 0 && current->type == Undo_Boundary) {
    ++nrecords;
    --undo->top;
    current = &undo->records[undo->top];
  }

  while (undo->top > 0 && current->type != Undo_Boundary) {
    ++nrecords;
    --undo->top;
    current = &undo->records[undo->top];
  }

  if (nrecords > 0) {
    *records_out = calloc(nrecords, sizeof(struct undo_record));
    *nrecords_out = nrecords;

    struct undo_record *dest = *records_out;

    // copy backwards
    for (uint32_t reci = undo->top + nrecords, outi = 0; reci > undo->top;
         --reci, ++outi) {
      dest[outi] = undo->records[reci - 1];
    }
  }

  if (undo->top > 0) {
    --undo->top;
  } else {
    undo->top = INVALID_TOP;
  }
}

void undo_end(struct undo_stack *undo) { undo->undo_in_progress = false; }

uint32_t undo_size(struct undo_stack *undo) { return undo->nrecords; }
uint32_t undo_current_position(struct undo_stack *undo) { return undo->top; }

size_t rec_to_str(struct undo_record *rec, char *buffer, size_t n) {
  switch (rec->type) {
  case Undo_Add:
    return snprintf(buffer, n, "add { begin: (%d, %d) end: (%d, %d)}",
                    rec->add.begin.row, rec->add.begin.col, rec->add.end.row,
                    rec->add.end.col);
  case Undo_Delete:
    return snprintf(buffer, n, "delete { pos: (%d, %d), ptr: 0x%p, nbytes: %d}",
                    rec->delete.pos.row, rec->delete.pos.col, rec->delete.data,
                    rec->delete.nbytes);
  default:
    return snprintf(buffer, n, "boundary { save_point: %s }",
                    rec->boundary.save_point ? "yes" : "no");
  }
}

const char *undo_dump(struct undo_stack *undo) {
  uint32_t left = 8192;
  const char *buf = malloc(left);
  char *pos = (char *)buf;
  pos[0] = '\0';

  char rec_buf[256];
  for (uint32_t reci = 0; reci < undo->nrecords && left > 0; ++reci) {
    struct undo_record *rec = &undo->records[reci];
    rec_to_str(rec, rec_buf, 256);
    uint32_t written = snprintf(pos, left, "%d: [%s]%s\n", reci, rec_buf,
                                reci == undo->top ? " <- top" : "");
    left = written > left ? 0 : left - written;
    pos += written;
  }

  return buf;
}

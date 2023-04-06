#include "undo.h"
#include "string.h"
#include "vec.h"

#include <stdio.h>
#include <stdlib.h>

void undo_init(struct undo_stack *undo, uint32_t initial_capacity) {
  undo->top = INVALID_TOP;
  undo->undo_in_progress = false;
  VEC_INIT(&undo->records, initial_capacity);
}

void undo_clear(struct undo_stack *undo) {
  undo->top = INVALID_TOP;
  VEC_CLEAR(&undo->records);
}

void undo_destroy(struct undo_stack *undo) {
  VEC_FOR_EACH(&undo->records, struct undo_record * rec) {
    if (rec->type == Undo_Delete && rec->delete.data != NULL &&
        rec->delete.nbytes > 0) {
      free(rec->delete.data);
    }
  }

  undo_clear(undo);

  VEC_DESTROY(&undo->records);
}

uint32_t undo_push_boundary(struct undo_stack *undo,
                            struct undo_boundary boundary) {

  // we can only have one save point
  if (boundary.save_point) {
    VEC_FOR_EACH(&undo->records, struct undo_record * rec) {
      if (rec->type == Undo_Boundary && rec->boundary.save_point) {
        rec->boundary.save_point = false;
      }
    }
  }

  VEC_APPEND(&undo->records, struct undo_record * rec);
  rec->type = Undo_Boundary;
  rec->boundary = boundary;

  if (!undo->undo_in_progress) {
    undo->top = VEC_SIZE(&undo->records) - 1;
  }

  return VEC_SIZE(&undo->records) - 1;
}

bool pos_equal(struct position *a, struct position *b) {
  return a->row == b->row && a->col == b->col;
}

uint32_t undo_push_add(struct undo_stack *undo, struct undo_add add) {

  // "compress"
  if (!VEC_EMPTY(&undo->records) &&
      VEC_BACK(&undo->records)->type == Undo_Add &&
      pos_equal(&VEC_BACK(&undo->records)->add.end, &add.begin)) {
    VEC_BACK(&undo->records)->add.end = add.end;
  } else {
    VEC_APPEND(&undo->records, struct undo_record * rec);
    rec->type = Undo_Add;
    rec->add = add;
  }

  if (!undo->undo_in_progress) {
    undo->top = VEC_SIZE(&undo->records) - 1;
  }

  return VEC_SIZE(&undo->records) - 1;
}

uint32_t undo_push_delete(struct undo_stack *undo, struct undo_delete delete) {
  VEC_APPEND(&undo->records, struct undo_record * rec);
  rec->type = Undo_Delete;
  rec->delete = delete;

  if (!undo->undo_in_progress) {
    undo->top = VEC_SIZE(&undo->records) - 1;
  }

  return VEC_SIZE(&undo->records) - 1;
}

void undo_begin(struct undo_stack *undo) { undo->undo_in_progress = true; }

void undo_next(struct undo_stack *undo, struct undo_record **records_out,
               uint32_t *nrecords_out) {
  *nrecords_out = 0;
  *records_out = NULL;

  if (VEC_EMPTY(&undo->records)) {
    return;
  }

  if (undo->top == INVALID_TOP) {
    // reset back to the top (redo)
    undo->top = VEC_SIZE(&undo->records) - 1;
  }

  uint32_t nrecords = 1;
  struct undo_record *current = &VEC_ENTRIES(&undo->records)[undo->top];

  // skip any leading boundaries
  while (undo->top > 0 && current->type == Undo_Boundary) {
    ++nrecords;
    --undo->top;
    current = &VEC_ENTRIES(&undo->records)[undo->top];
  }

  // find the next boundary
  while (undo->top > 0 && current->type != Undo_Boundary) {
    ++nrecords;
    --undo->top;
    current = &VEC_ENTRIES(&undo->records)[undo->top];
  }

  if (nrecords > 0) {
    *records_out = calloc(nrecords, sizeof(struct undo_record));
    *nrecords_out = nrecords;

    struct undo_record *dest = *records_out;

    // copy backwards
    for (uint32_t reci = undo->top + nrecords, outi = 0; reci > undo->top;
         --reci, ++outi) {
      dest[outi] = VEC_ENTRIES(&undo->records)[reci - 1];
    }
  }

  if (undo->top > 0) {
    --undo->top;
  } else {
    undo->top = INVALID_TOP;
  }
}

void undo_end(struct undo_stack *undo) { undo->undo_in_progress = false; }

uint32_t undo_size(struct undo_stack *undo) { return VEC_SIZE(&undo->records); }
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
  VEC_FOR_EACH_INDEXED(&undo->records, struct undo_record * rec, reci) {
    rec_to_str(rec, rec_buf, 256);
    uint32_t written = snprintf(pos, left, "%d: [%s]%s\n", reci, rec_buf,
                                reci == undo->top ? " <- top" : "");
    left = written > left ? 0 : left - written;
    pos += written;
  }

  return buf;
}

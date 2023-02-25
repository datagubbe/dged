#include "vec.h"
#include <stdbool.h>
#include <stdint.h>

enum undo_record_type {
  Undo_Boundary = 1,
  Undo_Add = 2,
  Undo_Delete = 3,
};

struct position {
  uint32_t row;
  uint32_t col;
};

struct undo_boundary {
  bool save_point;
};

struct undo_add {
  struct position begin;
  struct position end;
};

struct undo_delete {
  struct position pos;
  uint8_t *data;
  uint32_t nbytes;
};

struct undo_record {
  enum undo_record_type type;

  union {
    struct undo_boundary boundary;
    struct undo_add add;
    struct undo_delete delete;
  };
};

#define INVALID_TOP -1

struct undo_stack {
  VEC(struct undo_record) records;
  uint32_t top;
  bool undo_in_progress;
};

void undo_init(struct undo_stack *undo, uint32_t initial_capacity);
void undo_clear(struct undo_stack *undo);
void undo_destroy(struct undo_stack *undo);

uint32_t undo_push_boundary(struct undo_stack *undo,
                            struct undo_boundary boundary);

uint32_t undo_push_add(struct undo_stack *undo, struct undo_add add);
uint32_t undo_push_delete(struct undo_stack *undo, struct undo_delete delete);

void undo_begin(struct undo_stack *undo);
void undo_next(struct undo_stack *undo, struct undo_record **records_out,
               uint32_t *nrecords_out);
void undo_end(struct undo_stack *undo);

uint32_t undo_size(struct undo_stack *undo);
uint32_t undo_current_position(struct undo_stack *undo);
const char *undo_dump(struct undo_stack *undo);

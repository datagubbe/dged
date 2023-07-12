#include "location.h"

bool location_is_between(struct location location, struct location start,
                         struct location end) {
  if (location.line >= start.line && location.line <= end.line) {
    if (location.line == end.line && location.col <= end.col &&
        location.line == start.line && location.col >= start.col) {
      // only one line
      return true;
    } else if (location.line == start.line && location.line != end.line &&
               location.col >= start.col) {
      // we are on the first line
      return true;
    } else if (location.line == end.line && location.line != start.line &&
               location.col <= end.col) {
      // we are on the last line
      return true;
    } else if (location.line != end.line && location.line != start.line) {
      // we are on lines in between
      return true;
    }
  }
  return false;
}

int location_compare(struct location l1, struct location l2) {
  if (l1.line < l2.line) {
    return -1;
  } else if (l1.line > l2.line) {
    return 1;
  } else {
    if (l1.col < l2.col) {
      return -1;
    } else if (l1.col > l2.col) {
      return 1;
    } else {
      return 0;
    }
  }
}

struct region region_new(struct location begin, struct location end) {
  struct region reg = {.begin = begin, .end = end};

  if (end.line < begin.line ||
      (end.line == begin.line && end.col < begin.col)) {
    reg.begin = end;
    reg.end = begin;
  }

  return reg;
}

bool region_has_size(struct region region) {
  return region.end.line != region.begin.line ||
         (region.end.line == region.begin.line &&
          region.begin.col != region.end.col);
}

bool region_is_inside(struct region region, struct location location) {
  return location_is_between(location, region.begin, region.end);
}

bool region_is_inside_rect(struct region region, struct location location) {
  return location.line >= region.begin.line &&
         location.line <= region.end.line && location.col >= region.begin.col &&
         location.col <= region.end.col;
}

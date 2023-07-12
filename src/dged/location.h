#ifndef _LOCATION_H
#define _LOCATION_H

#include <stdbool.h>
#include <stdint.h>

/**
 * A location inside text.
 */
struct location {
  /** The line in the text (0..) */
  uint32_t line;

  /** The column in the text (0..) */
  uint32_t col;
};

/**
 * Is the location between two other locations.
 *
 * @param [in] location The location to test.
 * @param [in] l1 The first location.
 * @param [in] l2 The other location.
 * @returns True if @ref location is between @ref l1 and @ref l2.
 */
bool location_is_between(struct location location, struct location l1,
                         struct location l2);

/**
 * Compare two locations.
 *
 * @param [in] l1 The first location.
 * @param [in] l2 The second location.
 *
 * @returns -1 if @ref l1 is before @ref l2, 0 if @ref l1 is equal to @ref l2
 *          and +1 if @ref l1 is after @ref l2.
 */
int location_compare(struct location l1, struct location l2);

/**
 * A region (area) in text.
 */
struct region {
  /** The top left corner of the region. */
  struct location begin;

  /** The bottom right corner of the region. */
  struct location end;
};

/**
 * Create a new region.
 *
 * Note that if begin is after end, their order will be reversed.
 *
 * @param [in] begin The point in the text where this region begins.
 * @param [in] end The point in the text where this region ends.
 * @returns a new region.
 */
struct region region_new(struct location begin, struct location end);

/**
 * Is this region covering anything?
 *
 * @param [in] region The region to check.
 * @returns True if the region has a size > 0.
 */
bool region_has_size(struct region region);

/**
 * Is the location inside the region?
 *
 * @param [in] region The region to test.
 * @param [in] location The location to test.
 * @returns True if @ref location is inside @ref region.
 */
bool region_is_inside(struct region region, struct location location);

bool region_is_inside_rect(struct region region, struct location location);

#endif

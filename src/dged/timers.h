#ifndef _TIMERS_H
#define _TIMERS_H

#include <stdint.h>

#include "vec.h"

struct timer;

void timers_init(void);
void timers_start_frame(void);

typedef VEC(const struct timer *) timer_vec;

struct timer *timer_start(const char *name);
uint64_t timer_stop(struct timer *timer);
struct timer *timer_get(const char *name);
float timer_average(const struct timer *timer);
uint64_t timer_min(const struct timer *timer);
uint64_t timer_max(const struct timer *timer);
const char *timer_name(const struct timer *timer);

typedef void (*timer_callback)(const struct timer *timer, void *userdata);
void timers_for_each(timer_callback callback, void *userdata);

timer_vec timers_sorted();

void timers_end_frame(void);
void timers_destroy(void);

uint64_t instant_ns(void);

#endif

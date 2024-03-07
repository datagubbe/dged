#ifndef _TIMERS_H
#define _TIMERS_H

struct timer;

void timers_init();
void timers_start_frame();

struct timer *timer_start(const char *name);
void timer_stop(struct timer *timer);
struct timer *timer_get(const char *name);
float timer_average(const struct timer *timer);
const char *timer_name(const struct timer *timer);

typedef void (*timer_callback)(const struct timer *timer, void *userdata);
void timers_for_each(timer_callback callback, void *userdata);

void timers_end_frame();
void timers_destroy();

#endif

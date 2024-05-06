#include "timers.h"
#include "hash.h"
#include "hashmap.h"

#include <stdbool.h>
#include <string.h>
#include <time.h>

#define NUM_FRAME_SAMPLES 16

struct timer {
  char name[32];
  uint64_t max;
  uint64_t min;
  uint64_t samples[NUM_FRAME_SAMPLES];
  struct timespec started_at;
};

HASHMAP_ENTRY_TYPE(timer_entry, struct timer);

static struct timers {
  uint32_t frame_index;
  HASHMAP(struct timer_entry) timers;
} g_timers;

void timers_init() {
  HASHMAP_INIT(&g_timers.timers, 32, hash_name);
  g_timers.frame_index = 0;
}

void timers_destroy() { HASHMAP_DESTROY(&g_timers.timers); }

void timers_start_frame() {
  HASHMAP_FOR_EACH(&g_timers.timers, struct timer_entry * entry) {
    struct timer *timer = &entry->value;
    timer->samples[g_timers.frame_index] = 0;
  }
}

void timers_end_frame() {
  g_timers.frame_index = (g_timers.frame_index + 1) % NUM_FRAME_SAMPLES;
}

struct timer *timer_start(const char *name) {
  HASHMAP_GET(&g_timers.timers, struct timer_entry, name, struct timer * t);
  if (t == NULL) {
    HASHMAP_APPEND(&g_timers.timers, struct timer_entry, name,
                   struct timer_entry * tnew);
    struct timer *new_timer = &tnew->value;

    size_t namelen = strlen(name);
    namelen = namelen >= 32 ? 31 : namelen;
    memcpy(new_timer->name, name, namelen);
    new_timer->name[namelen] = '\0';
    new_timer->max = 0;
    new_timer->min = (uint64_t)-1;
    memset(new_timer->samples, 0, sizeof(uint64_t) * NUM_FRAME_SAMPLES);

    t = new_timer;
  }

  clock_gettime(CLOCK_MONOTONIC, &t->started_at);
  return t;
}

uint64_t timer_stop(struct timer *timer) {
  struct timespec end;
  clock_gettime(CLOCK_MONOTONIC, &end);
  uint64_t elapsed = ((uint64_t)end.tv_sec * 1e9 + (uint64_t)end.tv_nsec) -
                     ((uint64_t)timer->started_at.tv_sec * 1e9 +
                      (uint64_t)timer->started_at.tv_nsec);

  if (elapsed > timer->max) {
    timer->max = elapsed;
  }

  if (elapsed < timer->min) {
    timer->min = elapsed;
  }

  timer->samples[g_timers.frame_index] += elapsed;
  return elapsed;
}

struct timer *timer_get(const char *name) {
  HASHMAP_GET(&g_timers.timers, struct timer_entry, name, struct timer * t);
  return t;
}

float timer_average(const struct timer *timer) {
  uint64_t sum = 0;
  for (uint32_t samplei = 0; samplei < NUM_FRAME_SAMPLES; ++samplei) {
    sum += timer->samples[samplei];
  }

  return (float)sum / NUM_FRAME_SAMPLES;
}

uint64_t timer_max(const struct timer *timer) { return timer->max; }

uint64_t timer_min(const struct timer *timer) { return timer->min; }

const char *timer_name(const struct timer *timer) { return timer->name; }

void timers_for_each(timer_callback callback, void *userdata) {
  HASHMAP_FOR_EACH(&g_timers.timers, struct timer_entry * entry) {
    const struct timer *timer = &entry->value;
    callback(timer, userdata);
  }
}

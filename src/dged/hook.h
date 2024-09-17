#ifndef _HOOK_H
#define _HOOK_H

#include <stdint.h>

#include "vec.h"

/** Callback when removing hooks to clean up userdata */
typedef void (*remove_hook_cb)(void *userdata);

#define HOOK_IMPL(name, callback_type)                                         \
  struct name##_hook {                                                         \
    uint32_t id;                                                               \
    callback_type callback;                                                    \
    void *userdata;                                                            \
  };                                                                           \
                                                                               \
  typedef VEC(struct name##_hook) name##_hook_vec;                             \
                                                                               \
  static inline uint32_t insert_##name##_hook(                                 \
      name##_hook_vec *hooks, uint32_t *id, callback_type callback,            \
      void *userdata) {                                                        \
    uint32_t iid = ++(*id);                                                    \
    struct name##_hook hook = (struct name##_hook){                            \
        .id = iid,                                                             \
        .callback = callback,                                                  \
        .userdata = userdata,                                                  \
    };                                                                         \
    VEC_PUSH(hooks, hook);                                                     \
                                                                               \
    return iid;                                                                \
  }                                                                            \
                                                                               \
  static inline void remove_##name##_hook(name##_hook_vec *hooks, uint32_t id, \
                                          remove_hook_cb callback) {           \
    uint64_t found_at = (uint64_t) - 1;                                        \
    VEC_FOR_EACH_INDEXED(hooks, struct name##_hook *h, idx) {                  \
      if (h->id == id) {                                                       \
        if (callback != NULL) {                                                \
          callback(h->userdata);                                               \
        }                                                                      \
        found_at = idx;                                                        \
        break;                                                                 \
      }                                                                        \
    }                                                                          \
    if (found_at != (uint64_t) - 1) {                                          \
      if (found_at < VEC_SIZE(hooks) - 1) {                                    \
        VEC_SWAP(hooks, found_at, VEC_SIZE(hooks) - 1);                        \
      }                                                                        \
      VEC_POP(hooks, struct name##_hook removed);                              \
      (void)removed;                                                           \
    }                                                                          \
  }

#define HOOK_IMPL_NO_REMOVE(name, callback_type)                               \
  struct name##_hook {                                                         \
    uint32_t id;                                                               \
    callback_type callback;                                                    \
    void *userdata;                                                            \
  };                                                                           \
                                                                               \
  typedef VEC(struct name##_hook) name##_hook_vec;                             \
                                                                               \
  static inline uint32_t insert_##name##_hook(                                 \
      name##_hook_vec *hooks, uint32_t *id, callback_type callback,            \
      void *userdata) {                                                        \
    uint32_t iid = ++(*id);                                                    \
    struct name##_hook hook = (struct name##_hook){                            \
        .id = iid,                                                             \
        .callback = callback,                                                  \
        .userdata = userdata,                                                  \
    };                                                                         \
    VEC_PUSH(hooks, hook);                                                     \
                                                                               \
    return iid;                                                                \
  }

#define dispatch_hook(hooks, hook_type, ...)                                   \
  VEC_FOR_EACH(hooks, hook_type *h) { h->callback(__VA_ARGS__, h->userdata); }

#define dispatch_hook_no_args(hooks, hook_type)                                \
  VEC_FOR_EACH(hooks, hook_type *h) { h->callback(h->userdata); }

#endif

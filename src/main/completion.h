#ifndef _COMPLETION_H
#define _COMPLETION_H

#include "dged/location.h"

struct buffer;
struct buffers;

struct completion {
  const char *display;
  const char *insert;
  bool complete;
};

struct completion_context {
  struct buffer *buffer;
  const struct location location;
  const uint32_t max_ncompletions;
  struct completion *completions;
};

typedef uint32_t (*completion_fn)(struct completion_context ctx,
                                  void *userdata);

struct completion_provider {
  char name[16];
  completion_fn complete;
  void *userdata;
};

enum completion_trigger_kind {
  CompletionTrigger_Input = 0,
  CompletionTrigger_Char = 1,
};

struct completion_trigger {
  enum completion_trigger_kind kind;
  union {
    uint32_t c;
    uint32_t nchars;
  };
};

void init_completion(struct buffers *buffers);
void destroy_completion();

typedef void (*insert_cb)();

/**
 * Enable completions in the buffer @ref source.
 *
 * @param source [in] The buffer to provide completions for.
 * @param trigger [in] The completion trigger to use for this completion.
 * @param providers [in] The completion providers to use.
 * @param nproviders [in] The number of providers in @ref providers.
 */
void enable_completion(struct buffer *source, struct completion_trigger trigger,
                       struct completion_provider *providers,
                       uint32_t nproviders, insert_cb on_completion_inserted);

struct completion_provider path_provider();
struct completion_provider buffer_provider();

/**
 * Abort any active completion.
 */
void abort_completion();

/**
 * Is a completion currently showing?
 *
 * @returns True if the completion window is showing completions.
 */
bool completion_active();

/**
 * Disable completion for @ref buffer.
 *
 * @param buffer [in] Buffer to disable completions for.
 */
void disable_completion(struct buffer *buffer);

#endif

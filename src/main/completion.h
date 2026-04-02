#ifndef _COMPLETION_H
#define _COMPLETION_H

#include <stddef.h>

#include "dged/location.h"

/** @file completion.h
 * Auto-complete system.
 */

struct buffer;
struct buffers;
struct buffer_view;
struct commands;

typedef struct s8 str8;

typedef struct region (*completion_render_fn)(void *, struct buffer *);
typedef void (*completion_selected_fn)(void *, struct buffer_view *);
typedef void (*completion_cleanup_fn)(void *);
typedef str8 (*completion_filter_text_fn)(void *);
typedef str8 (*completion_sort_text_fn)(void *);

struct completion {
  void *data;
  completion_render_fn render;
  completion_selected_fn selected;
  completion_cleanup_fn cleanup;
  completion_filter_text_fn filter_text;
  completion_sort_text_fn sort_text;
};

typedef bool (*filter_fn)(struct s8 needle, struct s8 item, size_t *match_begin,
                          size_t *match_end, uint32_t *score);

typedef void (*add_completions)(struct s8, filter_fn filter,
                                struct completion *, size_t);

bool filter_startswith(struct s8 needle, struct s8 item, size_t *match_begin,
                       size_t *match_end, uint32_t *score);
bool filter_contains(struct s8 needle, struct s8 item, size_t *match_begin,
                     size_t *match_end, uint32_t *score);
bool filter_fuzzy(struct s8 needle, struct s8 item, size_t *match_begin,
                  size_t *match_end, uint32_t *score);

/**
 * Context for calculating completions.
 */
struct completion_context {
  /** The buffer to complete in. */
  struct buffer *buffer;

  /** The current location in the buffer. */
  struct location location;

  /** Callback for adding items to the completion list */
  add_completions add_completions;
};

/**
 * A function that provides completions.
 */
typedef void (*completion_fn)(struct completion_context ctx, bool deletion,
                              void *userdata);

typedef void (*provider_cleanup_fn)(void *);

/**
 * A completion provider.
 */
struct completion_provider {
  /** Name of the completion provider */
  char name[16];

  /** Completion function. Called to trigger retreival of new completions. */
  completion_fn complete;

  /** Cleanup function called when provider is destroyed. */
  provider_cleanup_fn cleanup;

  /** Userdata sent to @ref completion_provider.complete */
  void *userdata;
};

/**
 * Initialize the completion system.
 *
 */
void init_completion(struct buffers *buffers);

/**
 * Tear down the completion system.
 */
void destroy_completion(void);

/**
 * Enable completions in the buffer @p source.
 *
 * @param source [in] The buffer to provide completions for.
 * @param providers [in] The completion providers to use.
 * @param nproviders [in] The number of providers in @p providers.
 */
void add_completion_providers(struct buffer *source,
                              struct completion_provider *providers,
                              uint32_t nproviders);

/**
 * Trigger a completion at @ref at in @ref buffer.
 *
 * @param buffer [in] Buffer to complete in.
 * @param at [in] The location in @ref buffer to provide completions at.
 */
void complete(struct buffer *buffer, struct location at);

/**
 * Abort any active completion.
 */
void abort_completion(void);

/**
 * Is a completion currently showing?
 *
 * @returns True if the completion window is showing completions.
 */
bool completion_active(void);

/**
 * Get a pointer to the buffer used to hold completion items.
 *
 * @returns A pointer to the buffer holding completions.
 */
struct buffer *completion_buffer(void);

/**
 * Disable completion for @ref buffer, removing all providers.
 *
 * @param buffer [in] Buffer to disable completions for.
 */
void disable_completion(struct buffer *buffer);

void pause_completion();
void resume_completion();

#endif

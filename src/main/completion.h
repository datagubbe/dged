#ifndef _COMPLETION_H
#define _COMPLETION_H

#include "dged/location.h"

/** @file completion.h
 * Auto-complete system.
 */

struct buffer;
struct buffers;
struct commands;

/**
 * A single completion.
 */
struct completion {
  /** The display text for the completion. */
  const char *display;

  /** The text to insert for this completion. */
  const char *insert;

  /**
   * True if this completion item represent a fully expanded value.
   *
   * One example might be when the file completion represents a
   * file (and not a directory) which means that there is not
   * going to be more to complete after picking this completion
   * item.
   */
  bool complete;
};

/**
 * Context for calculating completions.
 */
struct completion_context {
  /** The buffer to complete in. */
  struct buffer *buffer;

  /** The current location in the buffer. */
  const struct location location;

  /** The capacity of @ref completion_context.completions. */
  const uint32_t max_ncompletions;

  /** The resulting completions */
  struct completion *completions;
};

/**
 * A function that provides completions.
 */
typedef uint32_t (*completion_fn)(struct completion_context ctx,
                                  void *userdata);

/**
 * A completion provider.
 */
struct completion_provider {
  /** Name of the completion provider */
  char name[16];

  /** Completion function. Called to get new completions. */
  completion_fn complete;

  /** Userdata sent to @ref completion_provider.complete */
  void *userdata;
};

/**
 * Type of event that triggers a completion.
 */
enum completion_trigger_kind {
  /** Completion is triggered on any input. */
  CompletionTrigger_Input = 0,

  /** Completion is triggered on a specific char. */
  CompletionTrigger_Char = 1,
};

/**
 * Description for @c CompletionTrigger_Input.
 */
struct completion_trigger_input {
  /** Trigger completion after this many chars */
  uint32_t nchars;

  /** Trigger an initial complete? */
  bool trigger_initially;
};

/**
 * Completion trigger descriptor.
 */
struct completion_trigger {
  /** Type of trigger. */
  enum completion_trigger_kind kind;
  union {
    uint32_t c;
    struct completion_trigger_input input;
  };
};

/**
 * Initialize the completion system.
 *
 * @param buffers The buffer list to complete from.
 * @param commands The command list to complete from.
 */
void init_completion(struct buffers *buffers, struct commands *commands);

/**
 * Tear down the completion system.
 */
void destroy_completion();

/**
 * Callback for completion inserted.
 */
typedef void (*insert_cb)();

/**
 * Enable completions in the buffer @p source.
 *
 * @param source [in] The buffer to provide completions for.
 * @param trigger [in] The completion trigger to use for this completion.
 * @param providers [in] The completion providers to use.
 * @param nproviders [in] The number of providers in @p providers.
 * @param on_completion_inserted [in] Callback to be called when a completion
 * has been inserted.
 */
void enable_completion(struct buffer *source, struct completion_trigger trigger,
                       struct completion_provider *providers,
                       uint32_t nproviders, insert_cb on_completion_inserted);

/**
 * Create a new path completion provider.
 *
 * This provider completes filesystem paths.
 * @returns A filesystem path @ref completion_provider.
 */
struct completion_provider path_provider();

/**
 * Create a new buffer completion provider.
 *
 * This provider completes buffer names from the
 * buffer list.
 * @returns A buffer name @ref completion_provider.
 */
struct completion_provider buffer_provider();

/**
 * Create a new command completion provider.
 *
 * This provider completes registered command names.
 * @returns A command name @ref completion_provider.
 */
struct completion_provider commands_provider();

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

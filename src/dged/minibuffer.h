#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

struct buffer;
struct command_ctx;
struct command_list;
struct keymap;

/**
 * Initialize the minibuffer.
 *
 * Note that the minibuffer is a global instance and this function will do
 * nothing if called more than once.
 * @param buffer underlying buffer to use for text IO in the minibuffer.
 */
void minibuffer_init(struct buffer *buffer);

/**
 * Destroy the minibuffer
 *
 * Note that this does not release the buffer used.
 */
void minibuffer_destroy();

struct text_chunk minibuffer_content();

struct buffer *minibuffer_buffer();

/**
 * Echo a message to the minibuffer.
 *
 * @param fmt Format string for the message.
 * @param ... Format arguments.
 */
void minibuffer_echo(const char *fmt, ...);

/**
 * Echo a message to the minibuffer that disappears after @ref timeout.
 *
 * @param timeout The timeout in seconds after which the message should
 * disappear.
 * @param fmt Format string for the message.
 * @param ... Format arguments.
 */
void minibuffer_echo_timeout(uint32_t timeout, const char *fmt, ...);

/**
 * Prompt for user input in the minibuffer.
 *
 * This will move focus to the minibuffer and wait for user input, with the
 * given prompt.
 * @param command_ctx The command context to use to re-execute the calling
 * command (or other command) when the user confirms the input.
 * @param fmt Format string for the prompt.
 * @param ... Format arguments.
 * @returns 0 on success.
 */
int32_t minibuffer_prompt(struct command_ctx command_ctx, const char *fmt, ...);

int32_t minibuffer_prompt_interactive(struct command_ctx command_ctx,
                                      void (*update_callback)(void *),
                                      void *userdata, const char *fmt, ...);

void minibuffer_set_prompt(const char *fmt, ...);
uint32_t minibuffer_draw_prompt(struct command_list *commands);

/**
 * Evaluate the current contents of the minibuffer
 *
 * @returns zero on success, non-zero to indicate failure
 */
int32_t minibuffer_execute();

/**
 * Abort the current minibuffer prompt.
 *
 * This returns focus to the previously focused window.
 */
void minibuffer_abort_prompt();

/**
 * Minibuffer prompt args
 */
struct minibuffer_prompt_args {
  int argc;
  const char **argv;
};

/**
 * Clear the current text in the minibuffer.
 */
void minibuffer_clear();

bool minibuffer_empty();

/**
 * Is the minibuffer currently displaying something?
 *
 * @returns True if the minibuffer is displaying anything, false otherwise.
 */
bool minibuffer_displaying();

/**
 * Is the minibuffer currently focused?
 *
 * @returns True if the minibuffer is currently focused, receiving user input.
 */
bool minibuffer_focused();

struct window *minibuffer_target_window();

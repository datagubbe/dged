/**
 * Commands and command registries
 */
#include <stdint.h>

struct buffer;
struct buffers;
struct window;

struct command_ctx {
  struct buffers *buffers;
  struct window *active_window;
  struct commands *commands;
  struct command *self;
  void *userdata;
};

typedef int32_t (*command_fn)(struct command_ctx ctx, int argc,
                              const char *argv[]);

struct command {
  const char *name;
  command_fn fn;
  void *userdata;
};

struct hashed_command {
  uint32_t hash;
  struct command command;
};

struct commands {
  struct hashed_command *commands;
  uint32_t ncommands;
  uint32_t capacity;
};

/**
 * Create a new command registry.
 *
 * @param[in] capacity The initial capacity for the registry
 */
struct commands command_registry_create(uint32_t capacity);

/**
 * Destroy a command registry.
 *
 * This will free all memory associated with stored commands.
 * @param[in] commands A pointer to a commands structure created by @ref
 * command_registry_create(uint32_t)
 */
void command_registry_destroy(struct commands *commands);

/**
 * Register a new command in the registry @ref commands.
 *
 * @param[in] commands The registry to insert into
 * @param[in] command The command to insert
 */
uint32_t register_command(struct commands *commands, struct command command);

/**
 * Register multiple commands in the registry @ref commands "command_list".
 *
 * @param[in] command_list The registry to insert into
 * @param[in] commands The commands to insert
 * @param[in] ncommands Number of commands contained in @ref commands
 */
void register_commands(struct commands *command_list, struct command *commands,
                       uint32_t ncommands);

/**
 * Execute a command and return the result.
 *
 * @param[in] command The @ref command to execute
 * @param[in] commands A @ref command "command registry" to use for context in
 * the executed command. Can for example be used to implement commands that
 * execute arbitrary other commands.
 * @param[in] active_window A @ref window representing the currently active
 * window in the editor. This provides a way to access the current buffer as
 * well.
 * @param[in] buffers The current list of buffers for context. Can be used for
 * example to create a buffer list.
 * @param[in] argc Number of arguments to the command.
 * @param[in] argv The arguments to the command.
 *
 * @returns Integer representing the exit status where 0 means success and
 * anything else means there was an error.
 */
int32_t execute_command(struct command *command, struct commands *commands,
                        struct window *active_window, struct buffers *buffers,
                        int argc, const char *argv[]);

/**
 * Hash the name of a command.
 *
 * @param[in] name The command name
 * @returns An integer representing the hash of the name
 */
uint32_t hash_command_name(const char *name);

/**
 * Lookup a command by name.
 *
 * @param[in] commands The @ref commands "command registry" to look for the @ref
 * command in.
 * @param[in] name The name of the command to look for
 * @returns A pointer to the command if found, NULL otherwise.
 */
struct command *lookup_command(struct commands *commands, const char *name);

/**
 * Lookup a command by hash.
 *
 * The hash value is expected to have been computed with @ref
 * hash_command_name(const char* name).
 *
 * @param[in] commands The @ref commands "command registry" to look for the @ref
 * command in.
 * @param[in] hash The hash value for the name of the command to look for
 * @returns A pointer to the command if found, NULL otherwise.
 */
struct command *lookup_command_by_hash(struct commands *commands,
                                       uint32_t hash);

/**
 *  @defgroup common-commands Implementation of common commands
 * @{
 */

/**
 * Find and visit a file in the current window.
 */
int32_t find_file(struct command_ctx ctx, int argc, const char *argv[]);

/**
 * Run a command interactively from the minibuffer.
 */
int32_t run_interactive(struct command_ctx ctx, int argc, const char *argv[]);

/**
 * Switch to another buffer in the currently active window
 */
int32_t switch_buffer(struct command_ctx ctx, int argc, const char *argv[]);

/**@}*/

#ifndef _COMMAND_H
#define _COMMAND_H

/** @file command.h
 * Commands and command registries
 */
#include "hashmap.h"
#include <stdint.h>

struct buffer;
struct buffers;
struct window;

/**
 * Execution context for a command
 */
struct command_ctx {
  /**
   * The current list of buffers.
   *
   * Can be used to insert new buffers or
   * inspect existing.
   */
  struct buffers *buffers;

  /**
   * The currently active window.
   */
  struct window *active_window;

  /**
   * A registry of available commands.
   *
   * Can be used to execute other commands as part of a command implementation.
   */
  struct commands *commands;

  /**
   * The command that is currently being executed
   */
  struct command *self;

  /**
   * User data set up by the command currently being executed.
   */
  void *userdata;

  const char *saved_argv[64];

  int saved_argc;
};

/** A command function callback which holds the implementation of a command */
typedef int32_t (*command_fn)(struct command_ctx ctx, int argc,
                              const char *argv[]);

/**
 * A command that can be bound to a key or executed directly
 */
struct command {
  /**
   * Name of the command
   *
   * Used to look the command up for execution and keybinds.
   */
  const char *name;

  /**
   * Implementation of command behavior
   */
  command_fn fn;

  /**
   * Userdata passed to each invocation of the command.
   */
  void *userdata;
};

/**
 * A command registry
 */
HASHMAP_ENTRY_TYPE(command_entry, struct command);

struct commands {
  HASHMAP(struct command_entry) commands;
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

void command_ctx_push_arg(struct command_ctx *ctx, const char *argv);
void command_ctx_free(struct command_ctx *ctx);

#endif

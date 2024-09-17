#ifndef _MAIN_COMPLETION_COMMAND_H
#define _MAIN_COMPLETION_COMMAND_H

struct command;
struct commands;

/**
 * Create a new command completion provider.
 *
 * This provider completes registered command names.
 * @returns A command name @ref completion_provider.
 */
struct completion_provider
create_commands_provider(struct commands *,
                         void (*on_command_selected)(struct command *));

#endif

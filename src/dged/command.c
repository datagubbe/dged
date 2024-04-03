#include "command.h"
#include "buffer.h"
#include "buffers.h"
#include "hash.h"
#include "hashmap.h"
#include "minibuffer.h"

#include <string.h>

struct commands command_registry_create(uint32_t capacity) {

  struct commands cmds = {0};
  HASHMAP_INIT(&cmds.commands, capacity, hash_name);
  return cmds;
}

void command_registry_destroy(struct commands *commands) {
  HASHMAP_DESTROY(&commands->commands);
}

uint32_t register_command(struct commands *commands, struct command command) {
  uint32_t hash = 0;
  HASHMAP_INSERT(&commands->commands, struct command_entry, command.name,
                 command, hash);
  return hash;
}

void register_commands(struct commands *command_list, struct command *commands,
                       uint32_t ncommands) {
  for (uint32_t ci = 0; ci < ncommands; ++ci) {
    register_command(command_list, commands[ci]);
  }
}

struct command *lookup_command(struct commands *command_list,
                               const char *name) {
  HASHMAP_GET(&command_list->commands, struct command_entry, name,
              struct command * command);
  return command;
}

struct command *lookup_command_by_hash(struct commands *commands,
                                       uint32_t hash) {
  HASHMAP_GET_BY_HASH(&commands->commands, struct command_entry, hash,
                      struct command * command);
  return command;
}

int32_t execute_command(struct command *command, struct commands *commands,
                        struct window *active_window, struct buffers *buffers,
                        int argc, const char *argv[]) {

  return command->fn(
      (struct command_ctx){
          .buffers = buffers,
          .active_window = active_window,
          .userdata = command->userdata,
          .commands = commands,
          .self = command,
          .saved_argv = {0},
          .saved_argc = 0,
      },
      argc, argv);
}

void commands_for_each(struct commands *commands,
                       void (*callback)(struct command *, void *),
                       void *userdata) {
  HASHMAP_FOR_EACH(&commands->commands, struct command_entry * entry) {
    callback(&entry->value, userdata);
  }
}

void command_ctx_push_arg(struct command_ctx *ctx, const char *argv) {
  if (ctx->saved_argc < 64) {
    ctx->saved_argv[ctx->saved_argc] = strdup(argv);
    ++ctx->saved_argc;
  }
}

void command_ctx_free(struct command_ctx *ctx) {
  for (uint32_t i = 0; i < ctx->saved_argc; ++i) {
    free((char *)ctx->saved_argv[i]);
  }

  ctx->saved_argc = 0;
}

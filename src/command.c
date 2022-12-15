#include "command.h"

#include <stdlib.h>

struct commands command_list_create(uint32_t capacity) {
  return (struct commands){
      .commands = calloc(capacity, sizeof(struct hashed_command)),
      .ncommands = 0,
      .capacity = capacity,
  };
}

void command_list_destroy(struct commands *commands) {
  free(commands->commands);
  commands->ncommands = 0;
  commands->capacity = 0;
}

uint32_t hash_command_name(const char *name) {
  unsigned long hash = 5381;
  int c;

  while ((c = *name++))
    hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

  return hash;
}

uint32_t register_command(struct commands *commands, struct command *command) {
  if (commands->ncommands == commands->capacity) {
    commands->capacity *= 2;
    commands->commands = realloc(
        commands->commands, sizeof(struct hashed_command) * commands->capacity);
  }

  uint32_t hash = hash_command_name(command->name);
  commands->commands[commands->ncommands] =
      (struct hashed_command){.command = command, .hash = hash};

  ++commands->ncommands;
  return hash;
}

void register_commands(struct commands *command_list, struct command *commands,
                       uint32_t ncommands) {
  for (uint32_t ci = 0; ci < ncommands; ++ci) {
    register_command(command_list, &commands[ci]);
  }
}

struct command *lookup_command(struct commands *command_list,
                               const char *name) {
  uint32_t needle = hash_command_name(name);
  return lookup_command_by_hash(command_list, needle);
}

struct command *lookup_command_by_hash(struct commands *commands,
                                       uint32_t hash) {
  for (uint32_t ci = 0; ci < commands->ncommands; ++ci) {
    if (commands->commands[ci].hash == hash) {
      return commands->commands[ci].command;
    }
  }

  return NULL;
}

int32_t execute_command(struct command *command, struct buffer *current_buffer,
                        int argc, const char *argv[]) {

  command->fn((struct command_ctx){.current_buffer = current_buffer,
                                   .userdata = command->userdata},
              argc, argv);

  // TODO
  return 0;
}

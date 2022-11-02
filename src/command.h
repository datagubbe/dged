#include <stdint.h>

struct buffer;

typedef void (*command_fn)(struct buffer *buffer);

struct command {
  const char *name;
  command_fn fn;
};

struct hashed_command {
  uint32_t hash;
  struct command *command;
};

struct commands {
  struct hashed_command *commands;
  uint32_t ncommands;
  uint32_t capacity;
};

struct commands command_list_create(uint32_t capacity);
void command_list_destroy(struct commands *commands);

uint32_t register_command(struct commands *commands, struct command *command);
void register_commands(struct commands *command_list, struct command *commands,
                       uint32_t ncommands);

uint32_t hash_command_name(const char *name);

struct command *lookup_command(struct commands *commands, const char *name);
struct command *lookup_command_by_hash(struct commands *commands,
                                       uint32_t hash);

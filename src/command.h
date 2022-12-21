#include <stdint.h>

struct buffer;

struct command_ctx {
  struct buffer *current_buffer;
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
  struct command *command;
};

struct commands {
  struct hashed_command *commands;
  uint32_t ncommands;
  uint32_t capacity;
};

struct commands command_registry_create(uint32_t capacity);
void command_registry_destroy(struct commands *commands);

uint32_t register_command(struct commands *commands, struct command *command);
void register_commands(struct commands *command_list, struct command *commands,
                       uint32_t ncommands);

int32_t execute_command(struct command *command, struct buffer *current_buffer,
                        int argc, const char *argv[]);

uint32_t hash_command_name(const char *name);

struct command *lookup_command(struct commands *commands, const char *name);
struct command *lookup_command_by_hash(struct commands *commands,
                                       uint32_t hash);

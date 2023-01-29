#include "buffer.h"
#include "buffers.h"
#include "minibuffer.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct hashed_command {
  uint32_t hash;
  struct command command;
};

struct commands command_registry_create(uint32_t capacity) {
  return (struct commands){
      .commands = calloc(capacity, sizeof(struct hashed_command)),
      .ncommands = 0,
      .capacity = capacity,
  };
}

void command_registry_destroy(struct commands *commands) {
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

uint32_t register_command(struct commands *commands, struct command command) {
  if (commands->ncommands == commands->capacity) {
    commands->capacity *= 2;
    commands->commands = realloc(
        commands->commands, sizeof(struct hashed_command) * commands->capacity);
  }

  uint32_t hash = hash_command_name(command.name);
  commands->commands[commands->ncommands] =
      (struct hashed_command){.command = command, .hash = hash};

  ++commands->ncommands;
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
  uint32_t needle = hash_command_name(name);
  return lookup_command_by_hash(command_list, needle);
}

struct command *lookup_command_by_hash(struct commands *commands,
                                       uint32_t hash) {
  for (uint32_t ci = 0; ci < commands->ncommands; ++ci) {
    if (commands->commands[ci].hash == hash) {
      return &commands->commands[ci].command;
    }
  }

  return NULL;
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
      },
      argc, argv);
}

int32_t find_file(struct command_ctx ctx, int argc, const char *argv[]) {
  const char *pth = NULL;
  if (argc == 1) {
    pth = argv[0];
    struct stat sb;
    if (stat(pth, &sb) < 0) {
      minibuffer_echo("stat on %s failed: %s", pth, strerror(errno));
      return 1;
    }

    if (S_ISDIR(sb.st_mode)) {
      minibuffer_echo("TODO: implement dired!");
      return 1;
    }

    window_set_buffer(ctx.active_window,
                      buffers_add(ctx.buffers, buffer_from_file((char *)pth)));
    minibuffer_echo_timeout(4, "buffer \"%s\" loaded",
                            ctx.active_window->buffer->name);
  } else {
    minibuffer_prompt(ctx, "find file: ");
  }

  return 0;
}

int32_t run_interactive(struct command_ctx ctx, int argc, const char *argv[]) {
  if (argc == 0) {
    minibuffer_prompt(ctx, "execute: ");
    return 0;
  }

  struct command *cmd = lookup_command(ctx.commands, argv[0]);
  if (cmd != NULL) {
    return execute_command(cmd, ctx.commands, ctx.active_window, ctx.buffers,
                           argc - 1, argv + 1);
  } else {
    minibuffer_echo_timeout(4, "command %s not found", argv[0]);
    return 11;
  }
}

int32_t do_switch_buffer(struct command_ctx ctx, int argc, const char *argv[]) {
  const char *bufname = argv[0];
  if (argc == 0) {
    // switch back to prev buffer
    if (ctx.active_window->prev_buffer != NULL) {
      bufname = ctx.active_window->prev_buffer->name;
    } else {
      return 0;
    }
  }

  struct buffer *buf = buffers_find(ctx.buffers, bufname);

  if (buf == NULL) {
    minibuffer_echo_timeout(4, "buffer %s not found", bufname);
    return 1;
  } else {
    window_set_buffer(ctx.active_window, buf);
    return 0;
  }
}

static struct command do_switch_buffer_cmd = {.fn = do_switch_buffer,
                                              .name = "do-switch-buffer"};

int32_t switch_buffer(struct command_ctx ctx, int argc, const char *argv[]) {
  if (argc == 0) {
    ctx.self = &do_switch_buffer_cmd;
    if (ctx.active_window->prev_buffer != NULL) {
      minibuffer_prompt(
          ctx, "buffer (default %s): ", ctx.active_window->prev_buffer->name);
    } else {
      minibuffer_prompt(ctx, "buffer: ");
    }
    return 0;
  }

  return execute_command(&do_switch_buffer_cmd, ctx.commands, ctx.active_window,
                         ctx.buffers, argc, argv);
}

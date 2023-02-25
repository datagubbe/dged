#include "command.h"
#include "buffer.h"
#include "buffers.h"
#include "hash.h"
#include "hashmap.h"
#include "minibuffer.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

int32_t find_file(struct command_ctx ctx, int argc, const char *argv[]) {
  const char *pth = NULL;
  if (argc == 0) {
    return minibuffer_prompt(ctx, "find file: ");
  }

  pth = argv[0];
  struct stat sb;
  if (stat(pth, &sb) < 0 && errno != ENOENT) {
    minibuffer_echo("stat on %s failed: %s", pth, strerror(errno));
    return 1;
  }

  if (S_ISDIR(sb.st_mode) && errno != ENOENT) {
    minibuffer_echo("TODO: implement dired!");
    return 1;
  }

  window_set_buffer(ctx.active_window,
                    buffers_add(ctx.buffers, buffer_from_file((char *)pth)));
  minibuffer_echo_timeout(4, "buffer \"%s\" loaded",
                          ctx.active_window->buffer->name);

  return 0;
}

int32_t write_file(struct command_ctx ctx, int argc, const char *argv[]) {
  const char *pth = NULL;
  if (argc == 0) {
    return minibuffer_prompt(ctx, "write to file: ");
  }

  pth = argv[0];
  buffer_write_to(ctx.active_window->buffer, pth);

  return 0;
}

int32_t run_interactive(struct command_ctx ctx, int argc, const char *argv[]) {
  if (argc == 0) {
    return minibuffer_prompt(ctx, "execute: ");
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
      return minibuffer_prompt(
          ctx, "buffer (default %s): ", ctx.active_window->prev_buffer->name);
    } else {
      return minibuffer_prompt(ctx, "buffer: ");
    }
  }

  return execute_command(&do_switch_buffer_cmd, ctx.commands, ctx.active_window,
                         ctx.buffers, argc, argv);
}

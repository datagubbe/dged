#include "command.h"

#include <stdbool.h>
#include <string.h>

#include "dged/buffer.h"
#include "dged/buffer_view.h"
#include "dged/command.h"
#include "dged/minibuffer.h"
#include "dged/utf8.h"

#include "main/completion.h"

static bool is_space(const struct codepoint *c) {
  // TODO: utf8 whitespace and other whitespace
  return c->codepoint == ' ';
}

typedef void (*on_command_selected_cb)(struct command *);

struct command_completion {
  struct command *command;
  on_command_selected_cb on_command_selected;
};

struct command_provider_data {
  struct commands *commands;
  on_command_selected_cb on_command_selected;
};

static void command_comp_selected(void *data, struct buffer_view *target) {
  struct command_completion *cc = (struct command_completion *)data;
  buffer_set_text(target->buffer, (uint8_t *)cc->command->name,
                  strlen(cc->command->name));

  abort_completion();
  cc->on_command_selected(cc->command);
}

static struct region command_comp_render(void *data,
                                         struct buffer *comp_buffer) {
  struct command *command = ((struct command_completion *)data)->command;
  struct location begin = buffer_end(comp_buffer);
  buffer_add(comp_buffer, buffer_end(comp_buffer), (uint8_t *)command->name,
             strlen(command->name));

  struct location end = buffer_end(comp_buffer);
  buffer_newline(comp_buffer, buffer_end(comp_buffer));

  return region_new(begin, end);
}

static void command_comp_cleanup(void *data) {
  struct command_completion *cc = (struct command_completion *)data;
  free(cc);
}

struct needle_match_ctx {
  const char *needle;
  struct completion *completions;
  uint32_t max_ncompletions;
  uint32_t ncompletions;
  on_command_selected_cb on_command_selected;
};

static void command_matches(struct command *command, void *userdata) {
  struct needle_match_ctx *ctx = (struct needle_match_ctx *)userdata;

  if (strncmp(ctx->needle, command->name, strlen(ctx->needle)) == 0 &&
      ctx->ncompletions < ctx->max_ncompletions) {

    struct command_completion *comp_data =
        calloc(1, sizeof(struct command_completion));
    comp_data->command = command;
    comp_data->on_command_selected = ctx->on_command_selected;
    ctx->completions[ctx->ncompletions] = (struct completion){
        .render = command_comp_render,
        .selected = command_comp_selected,
        .cleanup = command_comp_cleanup,
        .data = comp_data,
    };
    ++ctx->ncompletions;
  }
}

static void command_complete(struct completion_context ctx, bool deletion,
                             void *userdata) {
  (void)deletion;
  struct command_provider_data *pd = (struct command_provider_data *)userdata;
  struct commands *commands = pd->commands;
  if (commands == NULL) {
    return;
  }

  struct text_chunk txt = {0};
  if (ctx.buffer == minibuffer_buffer()) {
    txt = minibuffer_content();
  } else {
    struct match_result start =
        buffer_find_prev_in_line(ctx.buffer, ctx.location, is_space);
    if (!start.found) {
      start.at = (struct location){.line = ctx.location.line, .col = 0};
      return;
    }
    txt = buffer_region(ctx.buffer, region_new(start.at, ctx.location));
  }

  char *needle = calloc(txt.nbytes + 1, sizeof(char));
  memcpy(needle, txt.text, txt.nbytes);
  needle[txt.nbytes] = '\0';

  if (txt.allocated) {
    free(txt.text);
  }

  struct completion *completions = calloc(50, sizeof(struct completion));

  struct needle_match_ctx match_ctx = (struct needle_match_ctx){
      .needle = needle,
      .max_ncompletions = 50,
      .completions = completions,
      .ncompletions = 0,
      .on_command_selected = pd->on_command_selected,
  };

  commands_for_each(commands, command_matches, &match_ctx);
  ctx.add_completions(match_ctx.completions, match_ctx.ncompletions);
  free(completions);
  free(needle);
}

static void cleanup_provider(void *data) {
  struct command_provider_data *cpd = (struct command_provider_data *)data;
  free(cpd);
}

struct completion_provider
create_commands_provider(struct commands *commands,
                         on_command_selected_cb on_command_selected) {
  struct command_provider_data *data =
      calloc(1, sizeof(struct command_provider_data));
  data->commands = commands;
  data->on_command_selected = on_command_selected;

  return (struct completion_provider){
      .name = "commands",
      .complete = command_complete,
      .userdata = data,
      .cleanup = cleanup_provider,
  };
}

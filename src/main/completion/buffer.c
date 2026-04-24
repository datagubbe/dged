#include "buffer.h"

#include <string.h>

#include "completion/matchers.h"
#include "dged/buffer.h"
#include "dged/buffer_view.h"
#include "dged/buffers.h"
#include "dged/minibuffer.h"

#include "dged/vec.h"
#include "main/completion.h"

static bool is_space(const struct codepoint *c) {
  // TODO: utf8 whitespace and other whitespace
  return c->codepoint == ' ';
}

typedef void (*on_buffer_selected_cb)(struct buffer *);

struct buffer_completion {
  struct buffer *buffer;
  on_buffer_selected_cb on_buffer_selected;
  size_t match_begin;
  size_t match_end;
  size_t score;
};

struct buffer_provider_data {
  struct buffers *buffers;
  on_buffer_selected_cb on_buffer_selected;
};

static void buffer_comp_selected(void *data, struct buffer_view *target) {
  struct buffer_completion *bc = (struct buffer_completion *)data;
  buffer_set_text(target->buffer, (uint8_t *)bc->buffer->name,
                  strlen(bc->buffer->name));

  abort_completion();
  bc->on_buffer_selected(bc->buffer);
}

static struct region buffer_comp_render(void *data,
                                        struct buffer *comp_buffer) {
  struct buffer *buffer = ((struct buffer_completion *)data)->buffer;
  struct location begin = buffer_end(comp_buffer);
  buffer_add(comp_buffer, buffer_end(comp_buffer), (uint8_t *)buffer->name,
             strlen(buffer->name));

  struct location end = buffer_end(comp_buffer);
  buffer_newline(comp_buffer, buffer_end(comp_buffer));
  return region_new(begin, end);
}

static void buffer_comp_cleanup(void *data) {
  struct buffer_completion *bc = (struct buffer_completion *)data;
  free(bc);
}

typedef VEC(struct completion) completion_vec;

struct buffer_completions {
  completion_vec *completions;
  on_buffer_selected_cb on_buffer_selected;
  struct s8 needle;
};

static void fill_buffer_completions(struct buffer *buffer, void *userdata) {
  struct buffer_completions *ctx = (struct buffer_completions *)userdata;

  size_t match_begin, match_end;
  uint32_t score;
  if (filter_contains(ctx->needle, s8(buffer->name), &match_begin, &match_end,
                      &score)) {
    struct buffer_completion *comp_data =
        calloc(1, sizeof(struct buffer_completion));
    comp_data->buffer = buffer;
    comp_data->on_buffer_selected = ctx->on_buffer_selected;
    comp_data->match_begin = match_begin;
    comp_data->match_end = match_end;

    struct completion comp = (struct completion){
        .render = buffer_comp_render,
        .selected = buffer_comp_selected,
        .cleanup = buffer_comp_cleanup,
        .data = comp_data,
    };
    VEC_PUSH(ctx->completions, comp);
  }
}

static void buffer_complete(struct completion_context ctx, bool deletion,
                            void *userdata) {
  (void)deletion;
  struct buffer_provider_data *pd = (struct buffer_provider_data *)userdata;
  struct buffers *buffers = pd->buffers;
  if (buffers == NULL) {
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

  completion_vec completions;
  VEC_INIT(&completions, 32);
  struct buffer_completions match_ctx = (struct buffer_completions){
      .completions = &completions,
      .on_buffer_selected = pd->on_buffer_selected,
      .needle = s8(needle),
  };

  buffers_for_each(buffers, fill_buffer_completions, &match_ctx);
  free(needle);
  ctx.add_completions(VEC_ENTRIES(&completions), VEC_SIZE(&completions));
  VEC_DESTROY(&completions);
}

static void cleanup_provider(void *data) {
  struct buffer_provider_data *bpd = (struct buffer_provider_data *)data;
  free(bpd);
}

struct completion_provider
create_buffer_provider(struct buffers *buffers,
                       on_buffer_selected_cb on_buffer_selected) {
  struct buffer_provider_data *data =
      calloc(1, sizeof(struct buffer_provider_data));
  data->buffers = buffers;
  data->on_buffer_selected = on_buffer_selected;

  return (struct completion_provider){
      .name = "buffers",
      .complete = buffer_complete,
      .userdata = data,
      .cleanup = cleanup_provider,
  };
}

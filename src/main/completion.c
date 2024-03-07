#define _DEFAULT_SOURCE
#include "completion.h"

#include <dirent.h>
#include <errno.h>
#include <libgen.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "dged/binding.h"
#include "dged/buffer.h"
#include "dged/buffer_view.h"
#include "dged/buffers.h"
#include "dged/minibuffer.h"
#include "dged/path.h"
#include "dged/window.h"

#include "bindings.h"

struct active_completion_ctx {
  struct completion_trigger trigger;
  uint32_t trigger_current_nchars;
  struct completion_provider *providers;
  uint32_t nproviders;
  insert_cb on_completion_inserted;
};

struct completion_state {
  struct completion completions[50];
  uint32_t ncompletions;
  uint32_t current_completion;
  bool active;
  buffer_keymap_id keymap_id;
  bool keymap_active;
  struct active_completion_ctx *ctx;
} g_state = {0};

static struct buffer *g_target_buffer = NULL;

static void hide_completion();

static uint32_t complete_path(struct completion_context ctx, void *userdata);
static struct completion_provider g_path_provider = {
    .name = "path",
    .complete = complete_path,
    .userdata = NULL,
};

static uint32_t complete_buffers(struct completion_context ctx, void *userdata);
static struct completion_provider g_buffer_provider = {
    .name = "buffers",
    .complete = complete_buffers,
    .userdata = NULL,
};

struct completion_provider path_provider() {
  return g_path_provider;
}

struct completion_provider buffer_provider() {
  return g_buffer_provider;
}

struct active_completion {
  struct buffer *buffer;
  uint32_t insert_hook_id;
  uint32_t remove_hook_id;
};

VEC(struct active_completion) g_active_completions;

static int32_t goto_next_completion(struct command_ctx ctx, int argc,
                                    const char *argv[]) {
  if (g_state.current_completion < g_state.ncompletions - 1) {
    ++g_state.current_completion;
  }

  if (completion_active()) {
    buffer_view_goto(
        window_buffer_view(popup_window()),
        ((struct location){.line = g_state.current_completion, .col = 0}));
  }

  return 0;
}

static int32_t goto_prev_completion(struct command_ctx ctx, int argc,
                                    const char *argv[]) {
  if (g_state.current_completion > 0) {
    --g_state.current_completion;
  }

  if (completion_active()) {
    buffer_view_goto(
        window_buffer_view(popup_window()),
        ((struct location){.line = g_state.current_completion, .col = 0}));
  }

  return 0;
}

static int32_t insert_completion(struct command_ctx ctx, int argc,
                                 const char *argv[]) {
  // is it in the popup?
  struct completion *comp = &g_state.completions[g_state.current_completion];
  bool done = comp->complete;
  const char *ins = comp->insert;
  size_t inslen = strlen(ins);
  buffer_view_add(window_buffer_view(windows_get_active()), (uint8_t *)ins,
                  inslen);

  if (done) {
    g_state.ctx->on_completion_inserted();
    abort_completion();
  }

  return 0;
}

static void clear_completions() {
  for (uint32_t ci = 0; ci < g_state.ncompletions; ++ci) {
    free((void *)g_state.completions[ci].display);
    free((void *)g_state.completions[ci].insert);
    g_state.completions[ci].display = NULL;
    g_state.completions[ci].insert = NULL;
    g_state.completions[ci].complete = false;
  }
  g_state.ncompletions = 0;
}

COMMAND_FN("next-completion", next_completion, goto_next_completion, NULL);
COMMAND_FN("prev-completion", prev_completion, goto_prev_completion, NULL);
COMMAND_FN("insert-completion", insert_completion, insert_completion, NULL);

static void update_completions(struct buffer *buffer,
                               struct active_completion_ctx *ctx,
                               struct location location) {
  clear_completions();
  for (uint32_t pi = 0; pi < ctx->nproviders; ++pi) {
    struct completion_provider *provider = &ctx->providers[pi];

    struct completion_context comp_ctx = (struct completion_context){
        .buffer = buffer,
        .location = location,
        .max_ncompletions = 50 - g_state.ncompletions,
        .completions = g_state.completions,
    };

    g_state.ncompletions += provider->complete(comp_ctx, provider->userdata);
  }

  window_set_buffer_e(popup_window(), g_target_buffer, false, false);
  struct buffer_view *v = window_buffer_view(popup_window());

  size_t max_width = 0;
  uint32_t prev_selection = g_state.current_completion;

  buffer_clear(v->buffer);
  buffer_view_goto(v, (struct location){.line = 0, .col = 0});
  if (g_state.ncompletions > 0) {
    for (uint32_t compi = 0; compi < g_state.ncompletions; ++compi) {
      const char *disp = g_state.completions[compi].display;
      size_t width = strlen(disp);
      if (width > max_width) {
        max_width = width;
      }
      buffer_view_add(v, (uint8_t *)disp, width);
      buffer_view_add(v, (uint8_t *)"\n", 1);
    }

    // select the closest one to previous selection
    g_state.current_completion = prev_selection < g_state.ncompletions
                                     ? prev_selection
                                     : g_state.ncompletions - 1;

    buffer_view_goto(
        v, (struct location){.line = g_state.current_completion, .col = 0});

    struct window *target_window = window_find_by_buffer(buffer);
    struct window_position winpos = window_position(target_window);
    struct buffer_view *view = window_buffer_view(target_window);
    uint32_t height = g_state.ncompletions > 10 ? 10 : g_state.ncompletions;
    windows_show_popup(winpos.y + location.line - height - 1,
                       winpos.x + view->fringe_width + location.col + 1,
                       max_width + 2, height);

    if (!g_state.keymap_active) {
      struct keymap km = keymap_create("completion", 8);
      struct binding comp_bindings[] = {
          ANONYMOUS_BINDING(Ctrl, 'N', &next_completion_command),
          ANONYMOUS_BINDING(Ctrl, 'P', &prev_completion_command),
          ANONYMOUS_BINDING(ENTER, &insert_completion_command),
      };
      keymap_bind_keys(&km, comp_bindings,
                       sizeof(comp_bindings) / sizeof(comp_bindings[0]));
      g_state.keymap_id = buffer_add_keymap(buffer, km);
      g_state.keymap_active = true;
    }
  } else {
    hide_completion();
  }
}

static void on_buffer_delete(struct buffer *buffer, struct region deleted,
                             uint32_t start_idx, uint32_t end_idx,
                             void *userdata) {
  struct active_completion_ctx *ctx = (struct active_completion_ctx *)userdata;

  if (g_state.active) {
    update_completions(buffer, ctx, deleted.begin);
  }
}

static void on_buffer_insert(struct buffer *buffer, struct region inserted,
                             uint32_t start_idx, uint32_t end_idx,
                             void *userdata) {
  struct active_completion_ctx *ctx = (struct active_completion_ctx *)userdata;

  if (!g_state.active) {
    uint32_t nchars = 0;
    switch (ctx->trigger.kind) {
    case CompletionTrigger_Input:
      for (uint32_t line = inserted.begin.line; line <= inserted.end.line;
           ++line) {
        nchars += buffer_num_chars(buffer, line);
      }
      nchars -=
          inserted.begin.col +
          (buffer_num_chars(buffer, inserted.end.line) - inserted.end.col);

      ctx->trigger_current_nchars += nchars;

      if (ctx->trigger_current_nchars < ctx->trigger.input.nchars) {
        return;
      }

      ctx->trigger_current_nchars = 0;
      break;

    case CompletionTrigger_Char:
      // TODO
      break;
    }

    // activate completion
    g_state.active = true;
    g_state.ctx = ctx;
  }

  update_completions(buffer, ctx, inserted.end);
}

static void update_completion_buffer(struct buffer *buffer, void *userdata) {
  buffer_add_text_property(
      g_target_buffer,
      (struct location){.line = g_state.current_completion, .col = 0},
      (struct location){
          .line = g_state.current_completion,
          .col = buffer_num_chars(g_target_buffer, g_state.current_completion)},
      (struct text_property){.type = TextProperty_Colors,
                             .colors = (struct text_property_colors){
                                 .set_bg = false,
                                 .bg = 0,
                                 .set_fg = true,
                                 .fg = 4,
                             }});
}

void init_completion(struct buffers *buffers) {
  if (g_target_buffer == NULL) {
    g_target_buffer = buffers_add(buffers, buffer_create("*completions*"));
    buffer_add_update_hook(g_target_buffer, update_completion_buffer, NULL);
  }

  g_buffer_provider.userdata = buffers;
  VEC_INIT(&g_active_completions, 32);
}

struct oneshot_completion {
  uint32_t hook_id;
  struct active_completion_ctx *ctx;
};

static void cleanup_oneshot(void *userdata) { free(userdata); }

static void oneshot_completion_hook(struct buffer *buffer, void *userdata) {
  struct oneshot_completion *comp = (struct oneshot_completion *)userdata;

  // activate completion
  g_state.active = true;
  g_state.ctx = comp->ctx;

  struct window *w = window_find_by_buffer(buffer);
  if (w != NULL) {
    struct buffer_view *v = window_buffer_view(w);
    update_completions(buffer, comp->ctx, v->dot);
  } else {
    update_completions(buffer, comp->ctx,
                       (struct location){.line = 0, .col = 0});
  }

  // this is a oneshot after all
  buffer_remove_update_hook(buffer, comp->hook_id, cleanup_oneshot);
}

void enable_completion(struct buffer *source, struct completion_trigger trigger,
                       struct completion_provider *providers,
                       uint32_t nproviders, insert_cb on_completion_inserted) {
  // check if we are already active
  VEC_FOR_EACH(&g_active_completions, struct active_completion * c) {
    if (c->buffer == source) {
      disable_completion(source);
    }
  }

  struct active_completion_ctx *ctx =
      calloc(1, sizeof(struct active_completion_ctx));
  ctx->trigger = trigger;
  ctx->on_completion_inserted = on_completion_inserted;
  ctx->nproviders = nproviders;
  ctx->providers = calloc(nproviders, sizeof(struct completion_provider));
  memcpy(ctx->providers, providers,
         sizeof(struct completion_provider) * nproviders);

  uint32_t insert_hook_id =
      buffer_add_insert_hook(source, on_buffer_insert, ctx);
  uint32_t remove_hook_id =
      buffer_add_delete_hook(source, on_buffer_delete, ctx);

  VEC_PUSH(&g_active_completions, ((struct active_completion){
                                      .buffer = source,
                                      .insert_hook_id = insert_hook_id,
                                      .remove_hook_id = remove_hook_id,
                                  }));

  // do we want to trigger initially?
  if (ctx->trigger.kind == CompletionTrigger_Input &&
      ctx->trigger.input.trigger_initially) {
    struct oneshot_completion *comp =
        calloc(1, sizeof(struct oneshot_completion));
    comp->ctx = ctx;
    comp->hook_id =
        buffer_add_update_hook(source, oneshot_completion_hook, comp);
  }
}

static void hide_completion() {
  windows_close_popup();
  if (g_state.active) {
    buffer_remove_keymap(g_state.keymap_id);
    g_state.keymap_active = false;
  }
}

void abort_completion() {
  hide_completion();
  g_state.active = false;
  clear_completions();
}

bool completion_active() {
  return popup_window_visible() &&
         window_buffer(popup_window()) == g_target_buffer && g_state.active;
}

static void cleanup_active_comp_ctx(void *userdata) {
  struct active_completion_ctx *ctx = (struct active_completion_ctx *)userdata;
  free(ctx->providers);
  free(ctx);
}

static void do_nothing(void *userdata) {}

static void cleanup_active_completion(struct active_completion *comp) {
  buffer_remove_delete_hook(comp->buffer, comp->remove_hook_id, do_nothing);
  buffer_remove_insert_hook(comp->buffer, comp->insert_hook_id,
                            cleanup_active_comp_ctx);
}

void disable_completion(struct buffer *buffer) {
  VEC_FOR_EACH_INDEXED(&g_active_completions, struct active_completion * comp,
                       i) {
    if (buffer == comp->buffer) {
      VEC_SWAP(&g_active_completions, i, VEC_SIZE(&g_active_completions) - 1);
      VEC_POP(&g_active_completions, struct active_completion removed);
      cleanup_active_completion(&removed);
    }
  }
}

void destroy_completion() {
  // clean up any active completions we might have
  VEC_FOR_EACH(&g_active_completions, struct active_completion * comp) {
    cleanup_active_completion(comp);
  }
  VEC_DESTROY(&g_active_completions);
}

static bool is_hidden(const char *filename) {
  return filename[0] == '.' && filename[1] != '\0' && filename[1] != '.';
}

static int cmp_completions(const void *comp_a, const void *comp_b) {
  struct completion *a = (struct completion *)comp_a;
  struct completion *b = (struct completion *)comp_b;
  return strcmp(a->display, b->display);
}

static uint32_t complete_path(struct completion_context ctx, void *userdata) {

  // obtain path from the buffer
  struct text_chunk txt = {0};
  uint32_t start_idx = 0;
  if (ctx.buffer == minibuffer_buffer()) {
    txt = minibuffer_content();
  } else {
    txt = buffer_line(ctx.buffer, ctx.location.line);
    uint32_t end_idx = text_col_to_byteindex(
        ctx.buffer->text, ctx.location.line, ctx.location.col);

    for (uint32_t bytei = end_idx; bytei > 0; --bytei) {
      if (txt.text[bytei] == ' ') {
        start_idx = bytei + 1;
        break;
      }
    }

    if (start_idx >= end_idx) {
      return 0;
    }

    txt.nbytes = end_idx - start_idx;
  }

  char *path = calloc(txt.nbytes + 1, sizeof(uint8_t));
  memcpy(path, txt.text + start_idx, txt.nbytes);

  if (txt.allocated) {
    free(txt.text);
  }

  uint32_t n = 0;
  char *p1 = to_abspath(path);
  size_t len = strlen(p1);
  char *p2 = strdup(p1);

  size_t inlen = strlen(path);

  if (ctx.max_ncompletions == 0) {
    goto done;
  }

  const char *dir = p1;
  const char *file = "";

  // check the input path here since
  // to_abspath removes trailing slashes
  if (inlen == 0 || path[inlen - 1] != '/') {
    dir = dirname(p1);
    file = basename(p2);
  }

  DIR *d = opendir(dir);
  if (d == NULL) {
    goto done;
  }

  errno = 0;
  size_t filelen = strlen(file);
  bool file_is_curdir = (filelen == 1 && memcmp(file, ".", 1) == 0);
  while (n < ctx.max_ncompletions) {
    struct dirent *de = readdir(d);
    if (de == NULL && errno != 0) {
      // skip the erroring entry
      errno = 0;
      continue;
    } else if (de == NULL && errno == 0) {
      break;
    }

    switch (de->d_type) {
    case DT_DIR:
    case DT_REG:
    case DT_LNK:
      if (!is_hidden(de->d_name) &&
          (filelen == 0 || file_is_curdir ||
           (filelen <= strlen(de->d_name) &&
            memcmp(file, de->d_name, filelen) == 0))) {

        const char *disp = strdup(de->d_name);
        ctx.completions[n] = (struct completion){
            .display = disp,
            .insert = strdup(disp + (file_is_curdir ? 0 : filelen)),
            .complete = de->d_type == DT_REG,
        };
        ++n;
      }
      break;
    }
  }

  closedir(d);

done:
  free(path);
  free(p1);
  free(p2);

  qsort(ctx.completions, n, sizeof(struct completion), cmp_completions);
  return n;
}

struct buffer_match_ctx {
  const char *needle;
  struct completion *completions;
  uint32_t max_ncompletions;
  uint32_t ncompletions;
};

static void buffer_matches(struct buffer *buffer, void *userdata) {
  struct buffer_match_ctx *ctx = (struct buffer_match_ctx *)userdata;

  if (strncmp(ctx->needle, buffer->name, strlen(ctx->needle)) == 0 &&
      ctx->ncompletions < ctx->max_ncompletions) {
    ctx->completions[ctx->ncompletions] = (struct completion){
        .display = strdup(buffer->name),
        .insert = strdup(buffer->name + strlen(ctx->needle)),
        .complete = true,
    };
    ++ctx->ncompletions;
  }
}

static uint32_t complete_buffers(struct completion_context ctx,
                                 void *userdata) {
  struct buffers *buffers = (struct buffers *)userdata;
  if (buffers == NULL) {
    return 0;
  }

  struct text_chunk txt = {0};
  uint32_t start_idx = 0;
  if (ctx.buffer == minibuffer_buffer()) {
    txt = minibuffer_content();
  } else {
    txt = buffer_line(ctx.buffer, ctx.location.line);
    uint32_t end_idx = text_col_to_byteindex(
        ctx.buffer->text, ctx.location.line, ctx.location.col);
    for (uint32_t bytei = end_idx; bytei > 0; --bytei) {
      if (txt.text[bytei] == ' ') {
        start_idx = bytei + 1;
        break;
      }
    }

    if (start_idx >= end_idx) {
      return 0;
    }

    txt.nbytes = end_idx - start_idx;
  }

  char *needle = calloc(txt.nbytes + 1, sizeof(uint8_t));
  memcpy(needle, txt.text + start_idx, txt.nbytes);

  if (txt.allocated) {
    free(txt.text);
  }

  struct buffer_match_ctx match_ctx = (struct buffer_match_ctx){
      .needle = needle,
      .max_ncompletions = ctx.max_ncompletions,
      .completions = ctx.completions,
      .ncompletions = 0,
  };
  buffers_for_each(buffers, buffer_matches, &match_ctx);

  free(needle);
  return match_ctx.ncompletions;
}

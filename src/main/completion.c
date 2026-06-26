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
#include "dged/command.h"
#include "dged/display.h"
#include "dged/minibuffer.h"
#include "dged/path.h"
#include "dged/window.h"

#include "bindings.h"
#include "frame-hooks.h"

struct buffer_completion {
  struct buffer *buffer;
  uint32_t insert_hook_id;
  uint32_t remove_hook_id;

  VEC(struct completion_provider) providers;
};

struct completion_item {
  struct region area;
  struct completion completion;
};

static struct completion_state {
  VEC(struct buffer_completion) buffer_completions;
  VEC(struct completion_item) completions;
  struct buffer *completions_buffer;
  buffer_keymap_id keymap_id;
  struct buffer *target;
  layer_id highlight_current_layer;
  bool insert_in_progress;
  bool paused;
} g_state;

static uint64_t completion_index() {
  if (!completion_active()) {
    return 0;
  }

  struct buffer_view *view = window_buffer_view(popup_window());
  return view->dot.line;
}

static struct region active_completion_region(struct completion_state *state) {
  struct region reg =
      region_new((struct location){0, 0}, (struct location){0, 0});
  if (completion_index() < VEC_SIZE(&state->completions)) {
    reg = VEC_ENTRIES(&state->completions)[completion_index()].area;
  }

  return reg;
}

static int32_t goto_next_completion(struct command_ctx ctx, int argc,
                                    const char *argv[]) {
  (void)ctx;
  (void)argc;
  (void)argv;

  if (!completion_active()) {
    return 0;
  }

  struct buffer_view *view = window_buffer_view(popup_window());
  if (view->dot.line + 1 < VEC_SIZE(&g_state.completions)) {
    buffer_view_forward_line(view);
  }
  return 0;
}

static int32_t goto_prev_completion(struct command_ctx ctx, int argc,
                                    const char *argv[]) {
  (void)ctx;
  (void)argc;
  (void)argv;

  buffer_view_backward_line(window_buffer_view(popup_window()));
  return 0;
}

static int32_t insert_completion(struct command_ctx ctx, int argc,
                                 const char *argv[]) {
  (void)ctx;
  (void)argc;
  (void)argv;

  if (!completion_active()) {
    return 0;
  }

  struct buffer_view *bv = window_buffer_view(popup_window());
  struct window *target_window = windows_get_active();
  struct buffer_view *target = window_buffer_view(target_window);
  VEC_FOR_EACH(&g_state.completions, struct completion_item * item) {
    if (region_is_inside(item->area, bv->dot)) {
      g_state.insert_in_progress = true;
      item->completion.selected(item->completion.data, target);
      g_state.insert_in_progress = false;
      return 0;
    }
  }

  return 0;
}

static int32_t scroll_up_completions(struct command_ctx ctx, int argc,
                                     const char *argv[]) {
  if (!completion_active()) {
    return 0;
  }

  struct command *command = lookup_command(ctx.commands, "scroll-up");
  if (command != NULL) {
    return execute_command(command, ctx.commands, popup_window(), ctx.buffers,
                           ctx.display, argc, argv);
  }

  return 0;
}

static int32_t scroll_down_completions(struct command_ctx ctx, int argc,
                                       const char *argv[]) {
  if (!completion_active()) {
    return 0;
  }

  struct command *command = lookup_command(ctx.commands, "scroll-down");
  if (command != NULL) {
    return execute_command(command, ctx.commands, popup_window(), ctx.buffers,
                           ctx.display, argc, argv);
  }

  return 0;
}

static int32_t goto_first_completion(struct command_ctx ctx, int argc,
                                     const char *argv[]) {
  if (!completion_active()) {
    return 0;
  }

  struct command *command = lookup_command(ctx.commands, "goto-beginning");
  if (command != NULL) {
    return execute_command(command, ctx.commands, popup_window(), ctx.buffers,
                           ctx.display, argc, argv);
  }

  return 0;
}

static int32_t goto_last_completion(struct command_ctx ctx, int argc,
                                    const char *argv[]) {
  if (!completion_active()) {
    return 0;
  }

  struct command *command = lookup_command(ctx.commands, "goto-end");
  if (command != NULL) {
    return execute_command(command, ctx.commands, popup_window(), ctx.buffers,
                           ctx.display, argc, argv);
  }

  return 0;
}

COMMAND_FN("next-completion", next_completion, goto_next_completion, NULL)
COMMAND_FN("prev-completion", prev_completion, goto_prev_completion, NULL)
COMMAND_FN("insert-completion", insert_completion, insert_completion, NULL)
COMMAND_FN("scroll-up-completions", scroll_up_completions,
           scroll_up_completions, NULL);
COMMAND_FN("scroll-down-completions", scroll_down_completions,
           scroll_down_completions, NULL);
COMMAND_FN("goto-first-completion", goto_first_completion,
           goto_first_completion, NULL);
COMMAND_FN("goto-last-completion", goto_last_completion, goto_last_completion,
           NULL);

static void clear_completions(struct completion_state *state) {
  if (g_state.completions_buffer != NULL) {
    buffer_clear(state->completions_buffer);
  }

  VEC_FOR_EACH(&state->completions, struct completion_item * item) {
    if (item->completion.cleanup != NULL) {
      item->completion.cleanup(item->completion.data);
    }
  }

  VEC_CLEAR(&state->completions);
}

static void update_window_position(struct completion_state *state) {

  size_t ncompletions = VEC_SIZE(&state->completions);

  struct window *target_window = windows_get_active();
  struct window *root_wind = root_window();

  size_t nlines = buffer_num_lines(state->completions_buffer);
  size_t max_width = 10;

  window_set_buffer_e(popup_window(), state->completions_buffer, false, false);
  struct window_position winpos = window_position(target_window);
  struct buffer_view *view = window_buffer_view(target_window);
  uint32_t height = ncompletions > 10 ? 10 : ncompletions;

  size_t xpos =
      winpos.x + view->fringe_width + (view->dot.col - view->scroll.col) + 1;

  // should it be over or under?
  size_t relative_line = (view->dot.line - view->scroll.line);
  size_t ypos = winpos.y + relative_line;
  if (ypos > 10) {
    ypos -= height + 1;
  } else {
    ypos += 3;
  }

  for (uint64_t i = 0; i < nlines; ++i) {
    size_t linelen = buffer_line_length(state->completions_buffer, i);
    if (linelen > max_width) {
      max_width = linelen;
    }
  }

  size_t available = window_width(root_wind) - xpos - 5;
  max_width = max_width >= available ? available : max_width;

  // if we are opening anew, let's start on the first item
  if (!popup_window_visible()) {
    buffer_view_goto(window_buffer_view(popup_window()),
                     (struct location){0, 0});
  }

  windows_show_popup(ypos, xpos, max_width, height);
}

static void update_window_pos_frame_hook(void *data) {
  struct completion_state *state = (struct completion_state *)data;
  update_window_position(state);
}

static void open_completion(struct completion_state *state) {

  size_t ncompletions = VEC_SIZE(&state->completions);

  if (ncompletions == 0) {
    abort_completion();
    return;
  }

  struct window *target_window = windows_get_active();
  struct buffer *buffer = window_buffer(target_window);
  if (!completion_active() || state->target != buffer) {

    // clear any previous keymaps
    if (g_state.keymap_id != (uint64_t)-1) {
      buffer_remove_keymap(g_state.keymap_id);
    }

    g_state.keymap_id = (uint64_t)-1;

    struct keymap km = keymap_create("completion", 8);
    struct binding comp_bindings[] = {
        ANONYMOUS_BINDING(Ctrl, 'N', &next_completion_command),
        ANONYMOUS_BINDING(Ctrl, 'P', &prev_completion_command),
        ANONYMOUS_BINDING(DOWN, &next_completion_command),
        ANONYMOUS_BINDING(UP, &prev_completion_command),
        ANONYMOUS_BINDING(ENTER, &insert_completion_command),
        ANONYMOUS_BINDING(NUMPAD_ENTER, &insert_completion_command),

        ANONYMOUS_BINDING(Ctrl, 'V', &scroll_down_completions_command),
        ANONYMOUS_BINDING(Meta, 'v', &scroll_up_completions_command),
        ANONYMOUS_BINDING(Spec, '6', &scroll_down_completions_command),
        ANONYMOUS_BINDING(Spec, '5', &scroll_up_completions_command),

        ANONYMOUS_BINDING(Meta, '<', &goto_first_completion_command),
        ANONYMOUS_BINDING(Meta, '>', &goto_last_completion_command),
    };
    keymap_bind_keys(&km, comp_bindings,
                     sizeof(comp_bindings) / sizeof(comp_bindings[0]));

    state->keymap_id = buffer_add_keymap(buffer, km);
    state->target = buffer;
  }

  // need to run next frame to have the correct position
  run_next_frame(update_window_pos_frame_hook, state);
}

static void add_completions_impl(struct completion *completions,
                                 size_t ncompletions) {
  for (uint32_t i = 0; i < ncompletions; ++i) {
    struct completion *c = &completions[i];
    struct region area = c->render(c->data, g_state.completions_buffer);
    VEC_APPEND(&g_state.completions, struct completion_item * new);
    new->area = area;
    new->completion = *c;
  }

  open_completion(&g_state);
}

static void update_completions(struct completion_state *state,
                               struct buffer *buffer, struct location location,
                               bool deletion) {
  clear_completions(state);
  struct buffer_completion *buffer_config = NULL;
  VEC_FOR_EACH(&state->buffer_completions, struct buffer_completion * bc) {
    if (buffer == bc->buffer) {
      buffer_config = bc;
      break;
    }
  }

  if (buffer_config == NULL) {
    return;
  }

  VEC_FOR_EACH(&buffer_config->providers,
               struct completion_provider * provider) {
    struct completion_context comp_ctx = (struct completion_context){
        .buffer = buffer,
        .location = location,
        .add_completions = add_completions_impl,
    };

    provider->complete(comp_ctx, deletion, provider->userdata);
  }
}

static void update_comp_buffer(struct buffer *buffer, void *userdata) {
  struct completion_state *state = (struct completion_state *)userdata;

  buffer_clear_text_property_layer(buffer, state->highlight_current_layer);

  if (buffer_is_empty(buffer)) {
    abort_completion();
  }

  struct region reg = active_completion_region(state);
  if (region_has_size(reg)) {
    buffer_add_text_property_to_layer(buffer, reg.begin, reg.end,
                                      (struct text_property){
                                          .type = TextProperty_Colors,
                                          .data.colors =
                                              (struct text_property_colors){
                                                  .inverted = true,
                                                  .set_fg = false,
                                                  .set_bg = false,
                                                  .underline = false,
                                              },
                                      },
                                      state->highlight_current_layer);
  }
}

static void on_buffer_changed(struct buffer *buffer, struct edit_location edit,
                              bool deletion, void *userdata) {
  struct completion_state *state = (struct completion_state *)userdata;

  if (state->insert_in_progress || state->paused) {
    return;
  }

  update_completions(state, buffer, edit.coordinates.end, deletion);
}

static void on_buffer_insert(struct buffer *buffer, struct edit_location edit,
                             void *userdata) {
  on_buffer_changed(buffer, edit, false, userdata);
}

static void on_buffer_delete(struct buffer *buffer, struct edit_location edit,
                             void *userdata) {
  on_buffer_changed(buffer, edit, true, userdata);
}

static void on_buffer_destroy(struct buffer *buffer,
                              struct completion_state *state) {
  (void)state;

  // remove the buffer from our state
  disable_completion(buffer);
}

static void completions_buffer_deleted(struct buffer *buffer, void *userdata) {
  (void)buffer;
  struct completion_state *state = (struct completion_state *)userdata;
  state->completions_buffer = NULL;
}

void init_completion(struct buffers *buffers) {
  if (g_state.completions_buffer == NULL) {
    struct buffer b = buffer_create("*completions*");
    b.force_show_ws_off = true;
    b.retain_properties = true;
    g_state.completions_buffer = buffers_add(buffers, b);
  }

  g_state.highlight_current_layer =
      buffer_add_text_property_layer(g_state.completions_buffer);
  buffer_add_update_hook(g_state.completions_buffer, update_comp_buffer,
                         &g_state);

  buffer_add_destroy_hook(g_state.completions_buffer,
                          completions_buffer_deleted, &g_state);

  g_state.keymap_id = (uint64_t)-1;
  g_state.target = NULL;

  VEC_INIT(&g_state.buffer_completions, 50);
  VEC_INIT(&g_state.completions, 50);
  g_state.insert_in_progress = false;
  g_state.paused = false;
}

void add_completion_providers(struct buffer *source,
                              struct completion_provider *providers,
                              uint32_t nproviders) {

  struct buffer_completion *comp = NULL;
  VEC_FOR_EACH(&g_state.buffer_completions, struct buffer_completion * c) {
    if (c->buffer == source) {
      comp = c;
      break;
    }
  }

  if (comp == NULL) {
    VEC_APPEND(&g_state.buffer_completions,
               struct buffer_completion * new_comp);

    uint32_t insert_hook_id =
        buffer_add_insert_hook(source, on_buffer_insert, &g_state);
    uint32_t remove_hook_id =
        buffer_add_delete_hook(source, on_buffer_delete, &g_state);

    buffer_add_destroy_hook(source, (destroy_hook_cb)on_buffer_destroy,
                            &g_state);

    new_comp->buffer = source;
    new_comp->insert_hook_id = insert_hook_id;
    new_comp->remove_hook_id = remove_hook_id;
    VEC_INIT(&new_comp->providers, nproviders);
    comp = new_comp;
  }

  for (uint32_t i = 0; i < nproviders; ++i) {
    VEC_PUSH(&comp->providers, providers[i]);
  }
}

void complete(struct buffer *buffer, struct location at) {
  update_completions(&g_state, buffer, at, false);
}

void abort_completion(void) {
  windows_close_popup();

  if (g_state.keymap_id != (uint64_t)-1) {
    buffer_remove_keymap(g_state.keymap_id);
  }

  g_state.keymap_id = (uint64_t)-1;
  g_state.target = NULL;
}

bool completion_active(void) {
  return popup_window_visible() &&
         window_buffer(popup_window()) == g_state.completions_buffer;
}

static void do_nothing(void *userdata) { (void)userdata; }

static void cleanup_buffer_completion(struct buffer_completion *comp) {
  buffer_remove_delete_hook(comp->buffer, comp->remove_hook_id, do_nothing);
  buffer_remove_insert_hook(comp->buffer, comp->insert_hook_id, do_nothing);

  VEC_FOR_EACH(&comp->providers, struct completion_provider * provider) {
    if (provider->cleanup != NULL) {
      provider->cleanup(provider->userdata);
    }
  }

  VEC_DESTROY(&comp->providers);
}

void disable_completion(struct buffer *buffer) {
  VEC_FOR_EACH_INDEXED(&g_state.buffer_completions,
                       struct buffer_completion * comp, i) {
    if (buffer == comp->buffer) {
      VEC_SWAP(&g_state.buffer_completions, i,
               VEC_SIZE(&g_state.buffer_completions) - 1);
      VEC_POP(&g_state.buffer_completions, struct buffer_completion removed);
      cleanup_buffer_completion(&removed);
    }
  }
}

void destroy_completion(void) {
  clear_completions(&g_state);
  // clean up any active completions we might have
  VEC_FOR_EACH(&g_state.buffer_completions, struct buffer_completion * comp) {
    cleanup_buffer_completion(comp);
  }
  VEC_DESTROY(&g_state.buffer_completions);
  VEC_DESTROY(&g_state.completions);
}

void pause_completion() { g_state.paused = true; }

void resume_completion() { g_state.paused = false; }

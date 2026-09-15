#include "project-file.h"

#include <stdlib.h>
#include <unistd.h>

#include "completion.h"
#include "completion/matchers.h"
#include "dged/buffer.h"
#include "dged/buffer_view.h"
#include "dged/display.h"
#include "dged/minibuffer.h"
#include "dged/path.h"
#include "dged/process.h"
#include "dged/reactor.h"
#include "dged/s8.h"
#include "dged/text.h"
#include "dged/vec.h"

struct project_file_state {
  uint32_t stdout_event;

  struct process process;
  struct reactor *reactor;
  void (*on_complete)(void);

  struct s8 needle;

  struct s8 current_read;
  VEC(struct s8) files;

  add_completions add_callback;
  uint32_t buffer_update_hook_id;
};

struct project_file_completion {
  struct s8 file;

  struct match matches[32];
  size_t nmatches;

  void (*on_complete)(void);
};

static void project_file_selected(void *data, struct buffer_view *target) {
  struct project_file_completion *projfile_comp =
      (struct project_file_completion *)data;

  // replace all entered text with selected entry
  if (target->buffer == minibuffer_buffer()) {
    minibuffer_clear();
  } else {
    buffer_view_backward_word(target);
  }
  buffer_view_add(target, projfile_comp->file.s, projfile_comp->file.l);

  abort_completion();
  projfile_comp->on_complete();
}

static void project_file_cleanup(void *data) { free(data); }

static struct region project_file_render(void *data,
                                         struct buffer *comp_buffer) {
  struct project_file_completion *projfile_comp =
      (struct project_file_completion *)data;
  struct location start = buffer_end(comp_buffer);

  struct location at = buffer_add(comp_buffer, start, projfile_comp->file.s,
                                  projfile_comp->file.l);

  for (size_t i = 0; i < projfile_comp->nmatches; ++i) {
    struct match *m = &projfile_comp->matches[i];
    buffer_add_text_property(
        comp_buffer, (struct location){.col = m->begin, .line = start.line},
        (struct location){.col = m->end - 1, .line = start.line},
        (struct text_property){
            .type = TextProperty_Colors,
            .data.colors =
                (struct text_property_colors){
                    .set_fg = true,
                    .fg = Color_Cyan,
                },
        });
  }

  struct location end = at;
  at = buffer_newline(comp_buffer, at);

  return region_new(start, end);
}

static void fill_completions(struct project_file_state *state) {
  VEC(struct completion) completions;
  VEC_INIT(&completions, VEC_SIZE(&state->files));

  struct match matches[32] = {};
  size_t nmatches = 0;

  VEC_FOR_EACH(&state->files, struct s8 * f) {
    if (state->needle.l == 0 ||
        (nmatches = filter_fuzzy(state->needle, *f, matches, 32)) > 0) {
      VEC_APPEND(&completions, struct completion * comp);

      struct project_file_completion *data =
          calloc(1, sizeof(struct project_file_completion));
      data->file = *f;
      if (nmatches > 0) {
        memcpy(data->matches, matches, sizeof(struct match) * nmatches);
      }
      data->nmatches = nmatches;
      data->on_complete = state->on_complete;

      comp->data = data;
      comp->selected = project_file_selected;
      comp->cleanup = project_file_cleanup;
      comp->render = project_file_render;
    }
  }

  state->add_callback(VEC_ENTRIES(&completions), VEC_SIZE(&completions));
  VEC_DESTROY(&completions);
}

static void read_project_files(struct buffer *buffer, void *userdata) {
  struct project_file_state *state = (struct project_file_state *)userdata;

  uint8_t buf[4096];

  if (reactor_poll_event(state->reactor, state->stdout_event)) {
    ssize_t nread = 4096;
    while (nread == 4096) {
      ssize_t nread = read(state->process.stdout_, buf, 4096);

      // error
      if (nread < 0) {
        reactor_unregister_interest(state->reactor, state->stdout_event);
        state->stdout_event = (uint32_t)-1;

        buffer_remove_update_hook(buffer, state->buffer_update_hook_id, NULL);
        return;
      }

      // end of data
      if (nread == 0) {
        if (!s8empty(state->current_read)) {
          VEC_PUSH(&state->files, state->current_read);
          s8delete(state->current_read);
        }

        reactor_unregister_interest(state->reactor, state->stdout_event);
        state->stdout_event = (uint32_t)-1;
        fill_completions(state);

        buffer_remove_update_hook(buffer, state->buffer_update_hook_id, NULL);
        process_destroy(&state->process);
        return;
      }

      // parse lines
      size_t bytei = 0, total = nread, start = 0;
      while (bytei < total) {
        if (buf[bytei] == '\0') {
          if (!s8empty(state->current_read)) {
            struct s8 s =
                s8from_fmt("%s%s", s8ascstr(state->current_read), &buf[start]);
            VEC_PUSH(&state->files, s);
          } else {
            VEC_PUSH(&state->files,
                     s8new((const char *)&buf[start], bytei - start));
          }

          start = bytei + 1;
        }

        ++bytei;
      }

      if (bytei - start > 0) {
        s8delete(state->current_read);
        state->current_read = s8new((const char *)&buf[start], bytei - start);
      }
    }
  }
}

bool try_git(struct project_file_state *state, struct buffer *buffer) {
  bool success = false;
  struct s8 cwd = working_dir();
  struct s8 gitdata = join_path(cwd, s8(".git"));
  if (path_exists(gitdata)) {
    char *const cmd[] = {"git", "-C", (char *)s8ascstr(cwd), "ls-files", "-z"};
    struct process_create_result res = process_create(cmd, &state->process);
    if (!res.ok) {
      message("failed to run git to list project files");
    }

    if (state->stdout_event != (uint32_t)-1) {
      reactor_unregister_interest(state->reactor, state->stdout_event);
    }

    state->stdout_event = reactor_register_interest(
        state->reactor, state->process.stdout_, ReadInterest);

    state->buffer_update_hook_id =
        buffer_add_update_hook(buffer, read_project_files, state);
    success = true;
  }

  s8delete(cwd);
  s8delete(gitdata);

  return success;
}

static bool is_space(const struct codepoint *c) {
  // TODO: utf8 whitespace and other whitespace
  return c->codepoint == ' ';
}

static void project_file_complete(struct completion_context ctx, bool deletion,
                                  void *userdata) {

  (void)deletion;
  struct project_file_state *state = (struct project_file_state *)userdata;
  state->add_callback = ctx.add_completions;

  struct text_chunk txt;
  if (ctx.buffer == minibuffer_buffer()) {
    txt = minibuffer_content();
  } else {
    struct match_result start =
        buffer_find_prev_in_line(ctx.buffer, ctx.location, is_space);
    if (!start.found) {
      start.at = (struct location){.line = ctx.location.line, .col = 0};
    }
    txt = buffer_region(ctx.buffer, region_new(start.at, ctx.location));
  }

  s8delete(state->needle);
  state->needle = s8new((char *)txt.text, txt.nbytes);

  if (VEC_EMPTY(&state->files)) {
    try_git(state, ctx.buffer);
  } else {
    fill_completions(state);
  }
}

static void cleanup_project_file(void *data) {
  struct project_file_state *state = (struct project_file_state *)data;
  VEC_FOR_EACH(&state->files, struct s8 * f) { s8delete(*f); }
  VEC_DESTROY(&state->files);

  s8delete(state->needle);
  free(data);
}

struct completion_provider
create_project_file_provider(struct reactor *reactor,
                             void (*on_complete_path)(void)) {

  struct project_file_state *state =
      calloc(1, sizeof(struct project_file_state));

  state->stdout_event = (uint32_t)-1;
  state->on_complete = on_complete_path;
  state->reactor = reactor;
  state->current_read.l = 0;
  state->current_read.s = NULL;
  VEC_INIT(&state->files, 32);

  return (struct completion_provider){
      .name = "project-file",
      .complete = project_file_complete,
      .userdata = state,
      .cleanup = cleanup_project_file,
  };
}

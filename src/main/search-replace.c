#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "dged/binding.h"
#include "dged/buffer.h"
#include "dged/buffer_view.h"
#include "dged/command.h"
#include "dged/minibuffer.h"
#include "dged/window.h"

#include "bindings.h"
#include "search-replace.h"

static struct replace {
  char *replace;
  struct region *matches;
  uint32_t nmatches;
  uint32_t current_match;
} g_current_replace = {0};

void abort_replace() {
  reset_minibuffer_keys(minibuffer_buffer());
  free(g_current_replace.matches);
  free(g_current_replace.replace);
  g_current_replace.matches = NULL;
  g_current_replace.replace = NULL;
  g_current_replace.nmatches = 0;
  minibuffer_abort_prompt();
}

uint64_t matchdist(struct region *match, struct location loc) {
  struct location begin = match->begin;

  int64_t linedist = (int64_t)begin.line - (int64_t)loc.line;
  int64_t coldist = (int64_t)begin.col - (int64_t)loc.col;

  // arbitrary row scaling, best effort to avoid counting line length
  return (linedist * linedist) * 1e6 + coldist * coldist;
}

static void highlight_matches(struct buffer *buffer, struct region *matches,
                              uint32_t nmatches, uint32_t current) {
  for (uint32_t matchi = 0; matchi < nmatches; ++matchi) {
    struct region *m = &matches[matchi];
    if (matchi == current) {
      buffer_add_text_property(
          buffer, m->begin, m->end,
          (struct text_property){.type = TextProperty_Colors,
                                 .colors = (struct text_property_colors){
                                     .set_bg = true,
                                     .bg = 3,
                                     .set_fg = true,
                                     .fg = 0,
                                 }});

    } else {
      buffer_add_text_property(
          buffer, m->begin, m->end,
          (struct text_property){.type = TextProperty_Colors,
                                 .colors = (struct text_property_colors){
                                     .set_bg = true,
                                     .bg = 6,
                                     .set_fg = true,
                                     .fg = 0,
                                 }});
    }
  }
}

static int32_t replace_next(struct command_ctx ctx, int argc,
                            const char *argv[]) {
  struct replace *state = &g_current_replace;
  struct buffer_view *buffer_view = window_buffer_view(windows_get_active());

  struct region *m = &state->matches[state->current_match];
  buffer_view_set_mark_at(buffer_view, (struct location){.line = m->begin.line,
                                                         .col = m->begin.col});
  buffer_view_goto(buffer_view, (struct location){.line = m->end.line,
                                                  .col = m->end.col + 1});
  buffer_view_add(buffer_view, state->replace, strlen(state->replace));

  ++state->current_match;

  if (state->current_match == state->nmatches) {
    abort_replace();
  } else {
    m = &state->matches[state->current_match];
    buffer_view_goto(buffer_view, (struct location){.line = m->begin.line,
                                                    .col = m->begin.col});
    highlight_matches(buffer_view->buffer, state->matches, state->nmatches,
                      state->current_match);
  }

  return 0;
}

static int32_t skip_next(struct command_ctx ctx, int argc, const char *argv[]) {
  struct replace *state = &g_current_replace;

  struct buffer_view *buffer_view = window_buffer_view(windows_get_active());
  struct region *m = &state->matches[state->current_match];
  buffer_view_goto(buffer_view, (struct location){.line = m->end.line,
                                                  .col = m->end.col + 1});

  ++state->current_match;

  if (state->current_match == state->nmatches) {
    abort_replace();
  } else {
    m = &state->matches[state->current_match];
    buffer_view_goto(buffer_view, (struct location){.line = m->begin.line,
                                                    .col = m->begin.col});
    highlight_matches(buffer_view->buffer, state->matches, state->nmatches,
                      state->current_match);
  }

  return 0;
}

COMMAND_FN("replace-next", replace_next, replace_next, NULL);
COMMAND_FN("skip-next", skip_next, skip_next, NULL);

static int cmp_matches(const void *m1, const void *m2) {
  struct region *match1 = (struct region *)m1;
  struct region *match2 = (struct region *)m2;
  struct location dot = window_buffer_view(windows_get_active())->dot;
  uint64_t dist1 = matchdist(match1, dot);
  uint64_t dist2 = matchdist(match2, dot);

  int loc1 = location_compare(match1->begin, dot);
  int loc2 = location_compare(match2->begin, dot);

  int64_t score1 = dist1 * loc1;
  int64_t score2 = dist2 * loc2;

  if (score1 > 0 && score2 > 0) {
    return score1 < score2 ? -1 : score1 > score2 ? 1 : 0;
  } else if (score1 < 0 && score2 > 0) {
    return 1;
  } else if (score1 > 0 && score2 < 0) {
    return -1;
  } else { // both matches are behind dot
    return score1 < score2 ? 1 : score1 > score2 ? -1 : 0;
  }
}

static int32_t replace(struct command_ctx ctx, int argc, const char *argv[]) {
  if (argc == 0) {
    return minibuffer_prompt(ctx, "find: ");
  }

  if (argc == 1) {
    command_ctx_push_arg(&ctx, argv[0]);
    return minibuffer_prompt(ctx, "replace with: ");
  }

  struct buffer_view *buffer_view = window_buffer_view(windows_get_active());
  struct region *matches = NULL;
  uint32_t nmatches = 0;
  buffer_find(buffer_view->buffer, argv[0], &matches, &nmatches);

  if (nmatches == 0) {
    minibuffer_echo_timeout(4, "%s not found", argv[0]);
    free(matches);
    return 0;
  }

  // sort matches
  qsort(matches, nmatches, sizeof(struct region), cmp_matches);

  g_current_replace = (struct replace){
      .replace = strdup(argv[1]),
      .matches = matches,
      .nmatches = nmatches,
      .current_match = 0,
  };

  struct region *m = &g_current_replace.matches[0];
  buffer_view_goto(buffer_view, (struct location){.line = m->begin.line,
                                                  .col = m->begin.col});
  highlight_matches(buffer_view->buffer, g_current_replace.matches,
                    g_current_replace.nmatches, 0);

  struct binding bindings[] = {
      ANONYMOUS_BINDING(None, 'y', &replace_next_command),
      ANONYMOUS_BINDING(None, 'n', &skip_next_command),
      ANONYMOUS_BINDING(Ctrl, 'M', &replace_next_command),
  };
  buffer_bind_keys(minibuffer_buffer(), bindings,
                   sizeof(bindings) / sizeof(bindings[0]));

  return minibuffer_prompt(ctx, "replace? [yn] ");
}

static char *g_last_search = NULL;
static bool g_last_search_interactive = false;

const char *search_prompt(bool reverse) {
  const char *txt = "search (down): ";
  if (reverse) {
    txt = "search (up): ";
  }

  return txt;
}

struct closest_match {
  bool found;
  struct region closest;
};

static struct closest_match find_closest(struct buffer_view *view,
                                         const char *pattern, bool highlight,
                                         bool reverse) {
  struct region *matches = NULL;
  uint32_t nmatches = 0;
  struct closest_match res = {
      .found = false,
      .closest = {0},
  };

  buffer_find(view->buffer, pattern, &matches, &nmatches);

  // find the "nearest" match
  if (nmatches > 0) {
    res.found = true;
    struct region *closest = &matches[0];
    int64_t closest_dist = INT64_MAX;
    for (uint32_t matchi = 0; matchi < nmatches; ++matchi) {
      struct region *m = &matches[matchi];

      if (highlight) {
        buffer_add_text_property(
            view->buffer, m->begin, m->end,
            (struct text_property){.type = TextProperty_Colors,
                                   .colors = (struct text_property_colors){
                                       .set_bg = true,
                                       .bg = 6,
                                       .set_fg = true,
                                       .fg = 0,
                                   }});
      }
      int res = location_compare(m->begin, view->dot);
      uint64_t dist = matchdist(m, view->dot);
      if (((res < 0 && reverse) || (res > 0 && !reverse)) &&
          dist < closest_dist) {
        closest_dist = dist;
        closest = m;
      }
    }

    if (highlight) {
      buffer_add_text_property(
          view->buffer, closest->begin, closest->end,
          (struct text_property){.type = TextProperty_Colors,
                                 .colors = (struct text_property_colors){
                                     .set_bg = true,
                                     .bg = 3,
                                     .set_fg = true,
                                     .fg = 0,
                                 }});
    }
    res.closest = *closest;
  }

  free(matches);
  return res;
}

void do_search(struct buffer_view *view, const char *pattern, bool reverse) {
  g_last_search = strdup(pattern);

  struct closest_match m = find_closest(view, pattern, true, reverse);

  // find the "nearest" match
  if (m.found) {
    buffer_view_goto(view,
                     (struct location){.line = m.closest.begin.line,
                                       .col = m.closest.begin.col});
  } else {
    minibuffer_echo_timeout(4, "%s not found", pattern);
  }
}

int32_t search_interactive(struct command_ctx ctx, int argc,
                           const char *argv[]) {
  g_last_search_interactive = true;
  const char *pattern = NULL;
  if (minibuffer_content().nbytes == 0) {
    // recall the last search, if any
    if (g_last_search != NULL) {
      struct buffer_view *view = window_buffer_view(minibuffer_window());
      buffer_set_text(view->buffer, (uint8_t *)g_last_search, strlen(g_last_search));
      pattern = g_last_search;
    }
  } else {
    struct text_chunk content = minibuffer_content();
    char *p = malloc(content.nbytes + 1);
    strncpy(p, content.text, content.nbytes);
    p[content.nbytes] = '\0';
    pattern = p;
  }

  minibuffer_set_prompt(search_prompt(*(bool*)ctx.userdata));

  if (pattern != NULL) {
    do_search(window_buffer_view(minibuffer_target_window()), pattern,
              *(bool *)ctx.userdata);
    free((char *)pattern);
  }
  return 0;
}

static bool search_dir_backward = true;
static bool search_dir_forward = false;

COMMAND_FN("search-forward", search_forward, search_interactive,
           &search_dir_forward);
COMMAND_FN("search-backward", search_backward, search_interactive,
           &search_dir_backward);

int32_t find(struct command_ctx ctx, int argc, const char *argv[]) {
  bool reverse = *(bool *)ctx.userdata;
  if (argc == 0) {
    struct binding bindings[] = {
        ANONYMOUS_BINDING(Ctrl, 'S', &search_forward_command),
        ANONYMOUS_BINDING(Ctrl, 'R', &search_backward_command),
    };
    buffer_bind_keys(minibuffer_buffer(), bindings,
                     sizeof(bindings) / sizeof(bindings[0]));
    return minibuffer_prompt(ctx, search_prompt(reverse));
  }

  reset_minibuffer_keys(minibuffer_buffer());
  if (g_last_search_interactive) {
    g_last_search_interactive = false;
    return 0;
  }

  struct text_chunk content = minibuffer_content();
  char *pattern = malloc(content.nbytes + 1);
  strncpy(pattern, content.text, content.nbytes);
  pattern[content.nbytes] = '\0';

  do_search(window_buffer_view(ctx.active_window), pattern, reverse);
  free(pattern);

  return 0;
}

void register_search_replace_commands(struct commands *commands) {
  struct command search_replace_commands[] = {
      {.name = "find-next", .fn = find, .userdata = &search_dir_forward},
      {.name = "find-prev", .fn = find, .userdata = &search_dir_backward},
      {.name = "replace", .fn = replace},
  };

  register_commands(commands, search_replace_commands,
                    sizeof(search_replace_commands) /
                        sizeof(search_replace_commands[0]));
}

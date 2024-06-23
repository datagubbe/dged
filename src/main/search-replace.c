#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "dged/binding.h"
#include "dged/buffer.h"
#include "dged/buffer_view.h"
#include "dged/command.h"
#include "dged/minibuffer.h"
#include "dged/s8.h"
#include "dged/window.h"

#include "bindings.h"
#include "search-replace.h"

enum replace_state {
  Todo,
  Replaced,
  Skipped,
};

struct match {
  struct region region;
  enum replace_state state;
};

static struct replace {
  char *replace;
  struct match *matches;
  uint32_t nmatches;
  uint32_t current_match;
  buffer_keymap_id keymap_id;
  uint32_t highlight_hook;
  struct window *window;
} g_current_replace = {0};

static struct search {
  bool active;
  char *pattern;
  struct region *matches;
  struct buffer *buffer;
  uint32_t nmatches;
  uint32_t current_match;
  uint32_t highlight_hook;
  buffer_keymap_id keymap_id;
} g_current_search = {0};

static void clear_replace(void) {
  buffer_remove_keymap(g_current_replace.keymap_id);
  free(g_current_replace.matches);
  free(g_current_replace.replace);
  g_current_replace.matches = NULL;
  g_current_replace.replace = NULL;
  g_current_replace.nmatches = 0;

  if (g_current_replace.window != NULL) {
    buffer_remove_update_hook(window_buffer(g_current_replace.window),
                              g_current_replace.highlight_hook, NULL);
  }
  g_current_replace.highlight_hook = 0;
  g_current_replace.window = NULL;
}

void abort_replace(void) {
  clear_replace();
  minibuffer_abort_prompt();
}

static void clear_search(void) {
  // n.b. leak the pattern on purpose so
  // it can be used to recall previous searches.
  free(g_current_search.matches);
  g_current_search.matches = NULL;
  g_current_search.nmatches = 0;

  if (g_current_search.buffer != NULL &&
      g_current_search.highlight_hook != (uint32_t)-1) {
    buffer_remove_update_hook(g_current_search.buffer,
                              g_current_search.highlight_hook, NULL);
  }
  g_current_search.highlight_hook = -1;
  g_current_search.active = false;
}

void abort_search(void) {
  clear_search();

  buffer_remove_keymap(g_current_search.keymap_id);
  minibuffer_abort_prompt();
}

uint64_t matchdist(struct region *match, struct location loc) {
  struct location begin = match->begin;

  int64_t linedist = (int64_t)begin.line - (int64_t)loc.line;

  // if the match is on a different line, score it by how far
  // into the line it is, otherwise check the distance from location
  int64_t coldist = begin.col;
  if (linedist == 0) {
    int64_t coldist = (int64_t)begin.col - (int64_t)loc.col;
  }

  // arbitrary row scaling, best effort to avoid counting line length
  return (linedist * linedist) * 1e6 + coldist * coldist;
}

static void highlight_match(struct buffer *buffer, struct region match,
                            bool current) {
  if (current) {
    buffer_add_text_property(
        buffer, match.begin, match.end,
        (struct text_property){.type = TextProperty_Colors,
                               .colors = (struct text_property_colors){
                                   .set_bg = true,
                                   .bg = 3,
                                   .set_fg = true,
                                   .fg = 0,
                               }});

  } else {
    buffer_add_text_property(
        buffer, match.begin, match.end,
        (struct text_property){.type = TextProperty_Colors,
                               .colors = (struct text_property_colors){
                                   .set_bg = true,
                                   .bg = 6,
                                   .set_fg = true,
                                   .fg = 0,
                               }});
  }
}

static void search_highlight_hook(struct buffer *buffer, void *userdata) {
  (void)userdata;

  for (uint32_t matchi = 0; matchi < g_current_search.nmatches; ++matchi) {
    highlight_match(buffer, g_current_search.matches[matchi],
                    matchi == g_current_search.current_match);
  }
}

static void replace_highlight_hook(struct buffer *buffer, void *userdata) {
  (void)userdata;

  for (uint32_t matchi = 0; matchi < g_current_replace.nmatches; ++matchi) {
    struct match *m = &g_current_replace.matches[matchi];
    if (m->state != Todo) {
      continue;
    }

    highlight_match(buffer, m->region,
                    matchi == g_current_replace.current_match);
  }
}

static int32_t replace_next(struct command_ctx ctx, int argc,
                            const char *argv[]) {
  struct replace *state = &g_current_replace;
  struct buffer_view *buffer_view = window_buffer_view(state->window);
  struct buffer *buffer = buffer_view->buffer;

  struct match *match = &state->matches[state->current_match];

  // buffer_delete is not inclusive
  struct region to_delete = match->region;
  ++to_delete.end.col;

  struct location loc = buffer_delete(buffer, to_delete);
  struct location after = buffer_add(buffer, loc, (uint8_t *)state->replace,
                                     strlen(state->replace));
  match->state = Replaced;

  // update all following matches
  int64_t linedelta = (int64_t)after.line - (int64_t)to_delete.end.line;
  int64_t coldelta = linedelta == 0
                         ? (int64_t)after.col - (int64_t)to_delete.end.col
                         : (int64_t)after.col;
  for (uint32_t matchi = state->current_match; matchi < state->nmatches;
       ++matchi) {
    struct match *m = &state->matches[matchi];

    m->region.begin.line += linedelta;
    m->region.end.line += linedelta;

    if (after.line == m->region.begin.line) {
      m->region.begin.col += coldelta;
    }

    if (after.line == m->region.end.line) {
      m->region.end.col += coldelta;
    }
  }

  // advance to the next match
  ++state->current_match;
  if (state->current_match == state->nmatches) {
    abort_replace();
  } else {
    struct match *m = &state->matches[state->current_match];
    buffer_view_goto(buffer_view,
                     (struct location){.line = m->region.begin.line,
                                       .col = m->region.begin.col});
  }

  return 0;
}

static int32_t skip_next(struct command_ctx ctx, int argc, const char *argv[]) {
  struct replace *state = &g_current_replace;

  struct buffer_view *buffer_view = window_buffer_view(state->window);
  struct match *m = &state->matches[state->current_match];
  buffer_view_goto(buffer_view,
                   (struct location){.line = m->region.end.line,
                                     .col = m->region.end.col + 1});
  m->state = Skipped;

  ++state->current_match;

  if (state->current_match == state->nmatches) {
    abort_replace();
  } else {
    m = &state->matches[state->current_match];
    buffer_view_goto(buffer_view,
                     (struct location){.line = m->region.begin.line,
                                       .col = m->region.begin.col});
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
  } else {
    return score1 < score2 ? -1 : score1 > score2 ? 1 : 0;
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

  struct match *match_states = calloc(nmatches, sizeof(struct match));
  for (uint32_t matchi = 0; matchi < nmatches; ++matchi) {
    match_states[matchi].region = matches[matchi];
    match_states[matchi].state = Todo;
  }
  free(matches);

  g_current_replace = (struct replace){
      .replace = strdup(argv[1]),
      .matches = match_states,
      .nmatches = nmatches,
      .current_match = 0,
      .window = ctx.active_window,
  };

  // goto first match
  struct region *m = &g_current_replace.matches[0].region;
  buffer_view_goto(buffer_view, (struct location){.line = m->begin.line,
                                                  .col = m->begin.col});

  struct binding bindings[] = {
      ANONYMOUS_BINDING(None, 'y', &replace_next_command),
      ANONYMOUS_BINDING(None, 'n', &skip_next_command),
      ANONYMOUS_BINDING(Ctrl, 'M', &replace_next_command),
  };
  struct keymap km = keymap_create("replace", 8);
  keymap_bind_keys(&km, bindings, sizeof(bindings) / sizeof(bindings[0]));
  g_current_replace.keymap_id = buffer_add_keymap(minibuffer_buffer(), km);
  g_current_replace.highlight_hook =
      buffer_add_update_hook(buffer_view->buffer, replace_highlight_hook, NULL);

  return minibuffer_prompt(ctx, "replace? [yn] ");
}

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

static struct region *find_closest(struct region *matches, uint32_t nmatches,
                                   struct location dot, bool reverse,
                                   uint32_t *closest_idx) {
  struct region *closest = &matches[0];
  *closest_idx = 0;
  uint64_t closest_dist = UINT64_MAX;
  for (uint32_t matchi = 0; matchi < nmatches; ++matchi) {
    struct region *m = &matches[matchi];
    int res = location_compare(m->begin, dot);
    uint64_t dist = matchdist(m, dot);
    if (((res < 0 && reverse) || (res > 0 && !reverse)) &&
        dist < closest_dist) {
      closest_dist = dist;
      closest = m;
      *closest_idx = matchi;
    }
  }

  return closest;
}

static void do_search(struct buffer_view *view, const char *pattern,
                      bool reverse) {
  if (view->buffer != g_current_search.buffer) {
    clear_search();
  }

  g_current_search.buffer = view->buffer;
  g_current_search.active = true;

  // if we are in a new buffer, add the update hook for it.
  if (g_current_search.highlight_hook == (uint32_t)-1) {
    g_current_search.highlight_hook =
        buffer_add_update_hook(view->buffer, search_highlight_hook, NULL);
  }

  // replace the pattern if needed
  if (g_current_search.pattern == NULL ||
      !s8eq(s8(g_current_search.pattern), s8(pattern))) {
    char *new_pattern = strdup(pattern);
    free(g_current_search.pattern);
    g_current_search.pattern = new_pattern;
  }

  // clear out any old search results first
  if (g_current_search.matches != NULL) {
    free(g_current_search.matches);
    g_current_search.matches = NULL;
    g_current_search.nmatches = 0;
  }

  buffer_find(view->buffer, g_current_search.pattern, &g_current_search.matches,
              &g_current_search.nmatches);

  if (g_current_search.nmatches > 0) {
    // find the "nearest" match
    uint32_t closest_idx = 0;
    struct region *closest =
        find_closest(g_current_search.matches, g_current_search.nmatches,
                     view->dot, reverse, &closest_idx);
    buffer_view_goto(view, closest->begin);
    g_current_search.current_match = closest_idx;
  } else {
    abort_search();
    minibuffer_echo_timeout(4, "%s not found", pattern);
  }
}

static const char *get_pattern() {
  struct text_chunk content = minibuffer_content();
  char *p = malloc(content.nbytes + 1);
  memcpy(p, (const char *)content.text, content.nbytes);
  p[content.nbytes] = '\0';
  return (const char *)p;
}

int32_t search_interactive(struct command_ctx ctx, int argc,
                           const char *argv[]) {
  (void)argc;
  (void)argv;

  const char *pattern = NULL;
  if (minibuffer_content().nbytes == 0) {
    // recall the last search, if any
    if (g_current_search.pattern != NULL) {
      buffer_set_text(window_buffer(minibuffer_window()),
                      (uint8_t *)g_current_search.pattern,
                      strlen(g_current_search.pattern));
      pattern = strdup(g_current_search.pattern);
    }
  } else {
    pattern = get_pattern();
  }

  minibuffer_set_prompt(search_prompt(*(bool *)ctx.userdata));

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
    struct keymap m = keymap_create("search", 8);
    keymap_bind_keys(&m, bindings, sizeof(bindings) / sizeof(bindings[0]));
    g_current_search.keymap_id = buffer_add_keymap(minibuffer_buffer(), m);
    return minibuffer_prompt(ctx, search_prompt(reverse));
  }

  if (g_current_search.active) {
    abort_search();
    return 0;
  }

  buffer_remove_keymap(g_current_search.keymap_id);
  do_search(window_buffer_view(ctx.active_window), argv[0], reverse);

  if (g_current_search.active) {
    abort_search();
  }
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

void cleanup_search_replace(void) {
  clear_search();
  if (g_current_search.pattern != NULL) {
    free(g_current_search.pattern);
    g_current_search.pattern = NULL;
  }

  clear_replace();
}

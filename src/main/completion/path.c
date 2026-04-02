#define _DEFAULT_SOURCE
#include "path.h"

#include <dirent.h>
#include <errno.h>
#include <libgen.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "dged/buffer.h"
#include "dged/buffer_view.h"
#include "dged/display.h"
#include "dged/minibuffer.h"
#include "dged/path.h"
#include "dged/s8.h"
#include "dged/utf8.h"
#include "dged/vec.h"

#include "main/completion.h"

static bool is_space(const struct codepoint *c) {
  // TODO: utf8 whitespace and other whitespace
  return c->codepoint == ' ';
}

typedef void (*on_complete_path_cb)(void);

struct path_completion {
  struct s8 name;
  struct region replace;
  unsigned char type;
  on_complete_path_cb on_complete_path;
};

static void path_selected(void *data, struct buffer_view *target) {
  struct path_completion *comp_path = (struct path_completion *)data;
  struct location loc = buffer_delete(target->buffer, comp_path->replace);
  loc = buffer_add(target->buffer, loc, (uint8_t *)comp_path->name.s,
                   comp_path->name.l);
  buffer_view_goto(target, loc);
  switch (comp_path->type) {
  case DT_DIR:
    if (s8eq(comp_path->name, s8("."))) {
      // trigger "dired" in this case
      abort_completion();
      comp_path->on_complete_path();
      return;
    }

    buffer_view_add(target, (uint8_t *)"/", 1);
    break;
  default:
    break;
  }

  // if the user selected a "normal" file,
  // the completion is finished
  if (comp_path->type == DT_REG) {
    abort_completion();
    comp_path->on_complete_path();
  } else {
    complete(target->buffer, target->dot);
  }
}

static struct region path_render(void *data, struct buffer *comp_buffer) {
  struct path_completion *comp_path = (struct path_completion *)data;

  struct location start = buffer_end(comp_buffer);
  buffer_add(comp_buffer, buffer_end(comp_buffer), (uint8_t *)comp_path->name.s,
             comp_path->name.l);
  switch (comp_path->type) {
  case DT_DIR:
    if (!(s8eq(comp_path->name, s8(".")) || s8eq(comp_path->name, s8("..")))) {
      buffer_add(comp_buffer, buffer_end(comp_buffer), (uint8_t *)"/", 1);
      struct location end = buffer_end(comp_buffer);
      buffer_add_text_property(comp_buffer, start, end,
                               (struct text_property){
                                   .start = start,
                                   .end = end,
                                   .type = TextProperty_Colors,
                                   .data.colors =
                                       (struct text_property_colors){
                                           .set_fg = true,
                                           .fg = Color_Magenta,
                                       },
                               });
    }
    break;
  case DT_LNK: {
    struct location end = buffer_end(comp_buffer);
    buffer_add_text_property(comp_buffer, start, end,
                             (struct text_property){
                                 .start = start,
                                 .end = end,
                                 .type = TextProperty_Colors,
                                 .data.colors =
                                     (struct text_property_colors){
                                         .set_fg = true,
                                         .fg = Color_Green,
                                     },
                             });
  } break;
  default:
    break;
  }

  struct location end = buffer_end(comp_buffer);
  buffer_add(comp_buffer, buffer_end(comp_buffer), (uint8_t *)"\n", 1);

  return region_new(start, end);
}

static struct s8 path_filter_text(struct path_completion *path) {
  return path->name;
}

static void path_cleanup(void *data) {
  struct path_completion *comp_path = (struct path_completion *)data;
  s8delete(comp_path->name);
  free(comp_path);
}

static bool is_hidden(const char *filename) {
  return filename[0] == '.' && filename[1] != '\0' && filename[1] != '.';
}

static void path_complete(struct completion_context ctx, bool deletion,
                          void *on_complete_path) {
  (void)deletion;

  // obtain path from the buffer
  struct text_chunk txt = {0};
  struct location needle_end = ctx.location;
  if (ctx.buffer == minibuffer_buffer()) {
    txt = minibuffer_content();
    needle_end = buffer_end(minibuffer_buffer());
  } else {
    struct match_result start =
        buffer_find_prev_in_line(ctx.buffer, ctx.location, is_space);
    if (!start.found) {
      start.at = (struct location){.line = ctx.location.line, .col = 0};
      return;
    }
    txt = buffer_region(ctx.buffer, region_new(start.at, ctx.location));
  }

  char *path = calloc(txt.nbytes + 1, sizeof(char));
  memcpy(path, txt.text, txt.nbytes);
  path[txt.nbytes] = '\0';

  if (txt.allocated) {
    free(txt.text);
  }

  struct s8 p1 = expanduser(s8(path));
  struct s8 p2 = p1;
  p1 = canonicalize(p1);

  size_t inlen = strlen(path);

  const char *dir = s8ascstr(p1);
  const char *file = "";

  // check the input path here since
  // to_abspath removes trailing slashes
  if (inlen > 0 && path[inlen - 1] != '/') {
    dir = dirname((char *)s8ascstr(p1));
    file = basename((char *)s8ascstr(p2));
  }

  VEC(struct completion) completions;
  VEC_INIT(&completions, 32);

  DIR *d = opendir(dir);
  if (d == NULL) {
    goto done;
  }

  errno = 0;
  size_t filelen = strlen(file);
  size_t file_nchars = utf8_nchars((uint8_t *)file, filelen);
  struct location needle_start = (struct location){
      .line = needle_end.line,
      .col = needle_end.col - file_nchars,
  };

  while (true) {
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
      if (!is_hidden(de->d_name)) {

        struct path_completion *comp_data =
            calloc(1, sizeof(struct path_completion));
        comp_data->name = s8new(de->d_name, strlen(de->d_name));
        comp_data->replace = region_new(needle_start, needle_end);
        comp_data->type = de->d_type;
        comp_data->on_complete_path = on_complete_path;

        struct completion comp = {
            .data = comp_data,
            .render = path_render,
            .selected = path_selected,
            .cleanup = path_cleanup,
            .filter_text = (completion_filter_text_fn)path_filter_text,
            .sort_text = (completion_sort_text_fn)path_filter_text,
        };

        VEC_PUSH(&completions, comp);
      }
      break;
    }
  }

  closedir(d);

done:
  // qsort(completions, n, sizeof(struct completion), cmp_path_completions);
  ctx.add_completions(s8(file), filter_contains, VEC_ENTRIES(&completions),
                      VEC_SIZE(&completions));

  free(path);
  s8delete(p1);
  s8delete(p2);

  VEC_DESTROY(&completions);
}

struct completion_provider
create_path_provider(void (*on_complete_path)(void)) {
  return (struct completion_provider){
      .name = "path",
      .complete = path_complete,
      .userdata = on_complete_path,
  };
}

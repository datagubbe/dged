#include "binding.h"
#include "btree.h"
#include "buffer.h"
#include "buffer_view.h"
#include "command.h"
#include "display.h"
#include "minibuffer.h"

#include <math.h>

enum window_type {
  Window_Buffer,
  Window_HSplit,
  Window_VSplit,
};

struct window {
  uint32_t x;
  uint32_t y;
  uint32_t width;
  uint32_t height;
  enum window_type type;
  struct buffer_view buffer_view;
  struct buffer *prev_buffer;
  struct command_list *commands;
  uint32_t relline;
  uint32_t relcol;
};

BINTREE_ENTRY_TYPE(window_node, struct window);

static struct windows {
  BINTREE(window_node) windows;
  struct window *active;
  struct keymap keymap;
} g_windows;

static struct window g_minibuffer_window;

static struct window g_popup_window = {0};
static bool g_popup_visible = false;

void windows_init(uint32_t height, uint32_t width,
                  struct buffer *initial_buffer, struct buffer *minibuffer) {
  BINTREE_INIT(&g_windows.windows);

  g_minibuffer_window = (struct window){
      .buffer_view = buffer_view_create(minibuffer, false, false),
      .prev_buffer = NULL,
      .x = 0,
      .y = height - 1,
      .height = 1,
      .width = width,
  };

  struct window root_window = (struct window){
      .buffer_view = buffer_view_create(initial_buffer, true, true),
      .prev_buffer = NULL,
      .height = height - 1,
      .width = width,
      .x = 0,
      .y = 0,
  };
  BINTREE_SET_ROOT(&g_windows.windows, root_window);
  g_windows.active = &BINTREE_VALUE(BINTREE_ROOT(&g_windows.windows));
}

static void window_tree_clear_sub(struct window_node *root_node) {
  struct window_node *n = root_node;
  BINTREE_FIRST(n);
  while (n != NULL) {
    struct window *w = &BINTREE_VALUE(n);
    if (w->type == Window_Buffer) {
      buffer_view_destroy(&w->buffer_view);
    }
    BINTREE_NEXT(n);
  }
  BINTREE_FREE_NODES(root_node, window_node);
}

static void window_tree_clear() {
  window_tree_clear_sub(BINTREE_ROOT(&g_windows.windows));
}

void windows_destroy() { window_tree_clear(); }

struct window *root_window() {
  return &BINTREE_VALUE(BINTREE_ROOT(&g_windows.windows));
}

struct window *minibuffer_window() {
  return &g_minibuffer_window;
}

struct window *popup_window() {
  return &g_popup_window;
}

bool popup_window_visible() { return g_popup_visible; }

static void window_tree_resize(struct window_node *root, uint32_t height,
                               uint32_t width) {

  /* due to the way tree traversal works, we need to disconnect the subtree from
   * its potential parent. Otherwise the algorithm will traverse above the root
   * of the subtree. */
  struct window_node *orig_parent = BINTREE_PARENT(root);
  BINTREE_PARENT(root) = NULL;

  struct window *root_window = &BINTREE_VALUE(root);
  uint32_t original_root_width = root_window->width;
  uint32_t original_root_height = root_window->height;
  root_window->width = width;
  root_window->height = height;

  struct window_node *n = root;
  BINTREE_FIRST(n);
  while (n != NULL) {
    struct window *w = &BINTREE_VALUE(n);
    if (BINTREE_PARENT(n) != NULL && n != root) {
      if (BINTREE_LEFT(BINTREE_PARENT(n)) == n) {
        // if left child, use scale from root
        w->width = round(((float)w->width / (float)original_root_width) *
                         (float)root_window->width);
        w->height = round(((float)w->height / (float)original_root_height) *
                          (float)root_window->height);
      } else {
        // if right child, fill rest of space after left and parent resize
        struct window *left_sibling =
            &BINTREE_VALUE(BINTREE_LEFT(BINTREE_PARENT(n)));
        struct window *parent = &BINTREE_VALUE(BINTREE_PARENT(n));

        w->width = parent->width;
        w->height = parent->height;
        if (parent->type == Window_HSplit) {
          w->y = parent->y + left_sibling->height;
          w->height -= left_sibling->height;
        } else {
          w->x = parent->x + left_sibling->width;
          w->width -= left_sibling->width;
        }
      }
    }
    BINTREE_NEXT(n);
  }

  BINTREE_PARENT(root) = orig_parent;
}

void windows_resize(uint32_t height, uint32_t width) {
  g_minibuffer_window.width = width;
  g_minibuffer_window.y = height - 1;

  window_tree_resize(BINTREE_ROOT(&g_windows.windows), height - 1, width);
}

void windows_update(void *(*frame_alloc)(size_t), uint64_t frame_time) {

  struct window *w = &g_minibuffer_window;
  w->x = 0;
  w->commands = command_list_create(10, frame_alloc, w->x, w->y, "mb-prompt");

  // draw the prompt here to make it off-limits for the buffer/buffer view
  uint32_t prompt_len = minibuffer_draw_prompt(w->commands);
  w->x += prompt_len;
  uint32_t width = prompt_len < w->width ? w->width - prompt_len : 1;

  struct command_list *inner_commands = command_list_create(
      w->height * width, frame_alloc, w->x, w->y, "bufview-mb");

  struct buffer_view_update_params p = {
      .commands = inner_commands,
      .window_id = -1,
      .frame_time = frame_time,
      .width = width,
      .height = w->height,
      .window_x = w->x,
      .window_y = w->y,
      .frame_alloc = frame_alloc,
  };
  buffer_view_update(&w->buffer_view, &p);
  command_list_draw_command_list(w->commands, inner_commands);

  if (g_popup_visible) {
    w = &g_popup_window;

    const uint32_t hpadding = 1;
    const uint32_t border_width = 1;

    bool draw_padding = false, draw_borders = false;

    // first, make sure window fits
    struct window *rw = root_window();
    uint32_t w_x = w->x;
    uint32_t w_y = w->y;
    uint32_t width = w_x + w->width > rw->width ? rw->width - w_x : w->width;
    uint32_t height =
        w_y + w->height > rw->height ? rw->height - w_y : w->height;

    // is there space for padding?
    if (w_x > 1 && w_x + width + hpadding <= rw->width) {
      draw_padding = true;

      --w_x;
      width += hpadding * 2;
    }

    // is there space for borders?
    if (w_y > 1 && w_y + height <= rw->height && w_x > 1 &&
        w_x + width + border_width <= rw->width) {
      draw_borders = true;

      w_x -= border_width;
      // shift text upward
      w_y -= border_width * 2;
      height += border_width * 2;
      width += border_width * 2;
    }

    w->commands = command_list_create(height * width, frame_alloc, w_x, w_y,
                                      "popup-decor");
    uint32_t x = 0, y = 0;
    if (draw_borders) {
      // top
      command_list_draw_repeated(w->commands, x, y, 0x8c94e2, 1);
      command_list_draw_repeated(w->commands, x + 1, y, 0x8094e2,
                                 width - (border_width * 2));
      command_list_draw_repeated(w->commands, x + width - 1, y, 0x9094e2, 1);

      // sides
      for (uint32_t line = y + 1; line < (height + y - border_width); ++line) {
        command_list_draw_repeated(w->commands, x, line, 0x8294e2,
                                   border_width);
        command_list_draw_repeated(w->commands, x + width - border_width, line,
                                   0x8294e2, border_width);
      }

      // bottom
      command_list_draw_repeated(w->commands, x, y + height - border_width,
                                 0x9494e2, 1);
      command_list_draw_repeated(w->commands, x + 1, y + height - border_width,
                                 0x8094e2, width - (border_width * 2));
      command_list_draw_repeated(w->commands, x + width - 1,
                                 y + height - border_width, 0x9894e2, 1);

      x += border_width;
      y += border_width;
    }

    if (draw_padding) {
      for (uint32_t line = y; line < w->height + y; ++line) {
        command_list_draw_repeated(w->commands, x, line, ' ', hpadding);
        command_list_draw_repeated(w->commands, w->width + x + 1, line, ' ',
                                   hpadding);
      }
      x += border_width;
    }

    struct command_list *inner = command_list_create(
        w->height * w->width, frame_alloc, w_x + x, w_y + y, "bufview-popup");

    struct buffer_view_update_params p = {
        .commands = inner,
        .window_id = -1,
        .frame_time = frame_time,
        .width = w->width,
        .height = w->height,
        .window_x = w_x + x,
        .window_y = w_y + y,
        .frame_alloc = frame_alloc,
    };

    buffer_view_update(&w->buffer_view, &p);
    command_list_draw_command_list(w->commands, inner);
  }

  struct window_node *n = BINTREE_ROOT(&g_windows.windows);
  BINTREE_FIRST(n);
  uint32_t window_id = 0;
  while (n != NULL) {
    struct window *w = &BINTREE_VALUE(n);
    if (w->type == Window_Buffer) {
      char name[16];
      snprintf(name, 16, "bufview-%s", w->buffer_view.buffer->name);
      w->commands = command_list_create(w->height * w->width, frame_alloc, w->x,
                                        w->y, name);

      struct buffer_view_update_params p = {
          .commands = w->commands,
          .window_id = window_id,
          .frame_time = frame_time,
          .width = w->width,
          .height = w->height,
          .window_x = w->x,
          .window_y = w->y,
          .frame_alloc = frame_alloc,
      };

      buffer_view_update(&w->buffer_view, &p);
      ++window_id;
    }

    BINTREE_NEXT(n);
  }
}

void windows_render(struct display *display) {
  struct window_node *n = BINTREE_ROOT(&g_windows.windows);
  BINTREE_FIRST(n);
  while (n != NULL) {
    struct window *w = &BINTREE_VALUE(n);
    if (w->type == Window_Buffer) {
      display_render(display, w->commands);
    }
    BINTREE_NEXT(n);
  }

  display_render(display, g_minibuffer_window.commands);
  if (g_popup_visible) {
    display_render(display, g_popup_window.commands);
  }
}

struct window_node *find_window(struct window *window) {
  struct window_node *n = BINTREE_ROOT(&g_windows.windows);
  BINTREE_FIRST(n);
  while (n != NULL) {
    struct window *w = &BINTREE_VALUE(n);
    if (w == window) {
      return n;
    }
    BINTREE_NEXT(n);
  }

  return NULL;
}

void windows_set_active(struct window *window) {
  if (window == minibuffer_window()) {
    g_windows.active = minibuffer_window();
    return;
  }

  struct window_node *n = find_window(window);
  if (n != NULL) {
    g_windows.active = &BINTREE_VALUE(n);
  }
}

struct window *window_find_by_buffer(struct buffer *b) {
  if (b == window_buffer(minibuffer_window())) {
    return minibuffer_window();
  }

  struct window_node *n = BINTREE_ROOT(&g_windows.windows);
  BINTREE_FIRST(n);
  while (n != NULL) {
    struct window *w = &BINTREE_VALUE(n);
    if (window_buffer(w) == b) {
      return w;
    }
    BINTREE_NEXT(n);
  }

  return NULL;
}

struct window *windows_get_active() {
  return g_windows.active;
}

void window_set_buffer(struct window *window, struct buffer *buffer) {
  window_set_buffer_e(window, buffer, true, true);
}

void window_set_buffer_e(struct window *window, struct buffer *buffer,
                         bool modeline, bool line_numbers) {
  if (buffer != window->buffer_view.buffer) {
    window->prev_buffer = window->buffer_view.buffer;
    buffer_view_destroy(&window->buffer_view);
    window->buffer_view = buffer_view_create(buffer, modeline, line_numbers);
  }
}

struct buffer *window_buffer(struct window *window) {
  return window->buffer_view.buffer;
}

struct buffer_view *window_buffer_view(struct window *window) {
  return &window->buffer_view;
}

struct buffer *window_prev_buffer(struct window *window) {
  return window->prev_buffer;
}

bool window_has_prev_buffer(struct window *window) {
  return window->prev_buffer != NULL;
}

void window_close(struct window *window) {
  // do not want to delete last window
  if (window == root_window()) {
    return;
  }

  struct window_node *to_delete = find_window(window);
  if (to_delete == NULL) {
    return;
  }

  // promote other child to parent
  struct window_node *target = BINTREE_PARENT(to_delete);
  struct window_node *source = BINTREE_RIGHT(target) == to_delete
                                   ? BINTREE_LEFT(target)
                                   : BINTREE_RIGHT(target);

  buffer_view_destroy(&window->buffer_view);
  BINTREE_REMOVE(to_delete);
  BINTREE_FREE_NODE(to_delete);

  BINTREE_VALUE(source).x = BINTREE_VALUE(target).x;
  BINTREE_VALUE(source).y = BINTREE_VALUE(target).y;
  uint32_t target_width = BINTREE_VALUE(target).width;
  uint32_t target_height = BINTREE_VALUE(target).height;

  // copy the node value and set it's children as children of the target node
  BINTREE_VALUE(target) = BINTREE_VALUE(source);
  BINTREE_LEFT(target) = BINTREE_LEFT(source);
  BINTREE_RIGHT(target) = BINTREE_RIGHT(source);

  // adopt the children
  if (BINTREE_HAS_LEFT(source)) {
    BINTREE_PARENT(BINTREE_LEFT(source)) = target;
  }

  if (BINTREE_HAS_RIGHT(source)) {
    BINTREE_PARENT(BINTREE_RIGHT(source)) = target;
  }

  BINTREE_FREE_NODE(source);

  window_tree_resize(target, target_height, target_width);
  BINTREE_FIRST(target);
  windows_set_active(&BINTREE_VALUE(target));
}

void window_close_others(struct window *window) {
  struct window_node *root = BINTREE_ROOT(&g_windows.windows);

  // copy window and make it suitable as a root window
  struct window new_root = *window;
  new_root.x = 0;
  new_root.y = 0;
  new_root.buffer_view = buffer_view_clone(&window->buffer_view);
  new_root.width = BINTREE_VALUE(root).width;
  new_root.height = BINTREE_VALUE(root).height;

  window_tree_clear();

  // create new root window
  BINTREE_SET_ROOT(&g_windows.windows, new_root);
  windows_set_active(&BINTREE_VALUE(BINTREE_ROOT(&g_windows.windows)));
}

void window_hsplit(struct window *window, struct window **new_window_a,
                   struct window **new_window_b) {
  struct window_node *n = find_window(window);
  if (n != NULL) {
    struct window w = BINTREE_VALUE(n);

    if (w.type == Window_Buffer) {
      struct window parent = {0};
      parent.type = Window_HSplit;
      parent.x = w.x;
      parent.y = w.y;
      parent.width = w.width;
      parent.height = w.height;
      BINTREE_VALUE(n) = parent;

      /* Reuse the current window as the 'left' child, halving the height */
      w.height /= 2;
      BINTREE_INSERT(n, w);
      *new_window_a = &BINTREE_VALUE(BINTREE_LEFT(n));
      windows_set_active(*new_window_a);

      /* Create a new window for the split, showing the same buffer as the
       * original window.
       */
      struct window new_window = {0};
      new_window.type = Window_Buffer;
      new_window.buffer_view =
          buffer_view_create(w.buffer_view.buffer, true, true);
      buffer_view_goto(&new_window.buffer_view,
                       (struct location){.line = w.buffer_view.dot.line,
                                         .col = w.buffer_view.dot.col});
      new_window.prev_buffer = w.prev_buffer;
      new_window.x = w.x;
      new_window.y = w.y + w.height;
      new_window.width = w.width;
      new_window.height = parent.height - w.height;
      BINTREE_INSERT(n, new_window);
      *new_window_b = &BINTREE_VALUE(BINTREE_RIGHT(n));
    }
  }
}

void window_vsplit(struct window *window, struct window **new_window_a,
                   struct window **new_window_b) {
  struct window_node *n = find_window(window);
  if (n != NULL) {
    struct window w = BINTREE_VALUE(n);

    if (w.type == Window_Buffer) {
      /* Create a new split container to use as parent */
      struct window parent = {0};
      parent.type = Window_VSplit;
      parent.x = w.x;
      parent.y = w.y;
      parent.width = w.width;
      parent.height = w.height;
      BINTREE_VALUE(n) = parent;

      /* Reuse the current window as the 'left' child, halving the width */
      w.width /= 2;
      BINTREE_INSERT(n, w);
      *new_window_a = &BINTREE_VALUE(BINTREE_LEFT(n));
      windows_set_active(*new_window_a);

      /* Create a new window for the split, showing the same buffer as the
       * original window.
       */
      struct window new_window = {0};
      new_window.type = Window_Buffer;
      new_window.buffer_view =
          buffer_view_create(w.buffer_view.buffer, true, true);
      buffer_view_goto(&new_window.buffer_view,
                       (struct location){.line = w.buffer_view.dot.line,
                                         .col = w.buffer_view.dot.col});

      new_window.prev_buffer = w.prev_buffer;
      new_window.x = w.x + w.width;
      new_window.y = w.y;
      new_window.width = parent.width - w.width;
      new_window.height = w.height;
      BINTREE_INSERT(n, new_window);
      *new_window_b = &BINTREE_VALUE(BINTREE_RIGHT(n));
    }
  }
}

void window_split(struct window *window, struct window **new_window_a,
                  struct window **new_window_b) {
  /* The height * 2 is a horrible hack, we would need to know how big the font
     actually is */
  window->height * 2 > window->width
      ? window_hsplit(window, new_window_a, new_window_b)
      : window_vsplit(window, new_window_a, new_window_b);
}

struct window *windows_focus_next() {
  struct window *active = windows_get_active();
  struct window_node *n = find_window(active);
  BINTREE_NEXT(n);
  while (n != NULL) {
    struct window *w = &BINTREE_VALUE(n);
    if (w->type == Window_Buffer) {
      windows_set_active(w);
      return w;
    }
    BINTREE_NEXT(n);
  }

  // we have moved around
  n = BINTREE_ROOT(&g_windows.windows);
  BINTREE_FIRST(n);
  if (n != NULL) {
    windows_set_active(&BINTREE_VALUE(n));
    return &BINTREE_VALUE(n);
  }

  // fall back to root
  windows_set_active(root_window());
  return root_window();
}

struct window *windows_focus(uint32_t id) {
  uint32_t curr_id = 0;

  struct window_node *n = BINTREE_ROOT(&g_windows.windows);
  BINTREE_FIRST(n);
  while (n != NULL) {
    struct window *w = &BINTREE_VALUE(n);
    if (w->type == Window_Buffer) {
      if (curr_id == id) {
        windows_set_active(w);
        return w;
      }
      ++curr_id;
    }
    BINTREE_NEXT(n);
  }

  return NULL;
}

uint32_t window_width(const struct window *window) { return window->width; }

uint32_t window_height(const struct window *window) { return window->height; }

struct window_position window_position(const struct window *window) {
  return (struct window_position){.x = window->x, .y = window->y};
}

void windows_show_popup(uint32_t row, uint32_t col, uint32_t width,
                        uint32_t height) {
  g_popup_window.x = col;
  g_popup_window.y = row;
  g_popup_window.width = width;
  g_popup_window.height = height;
  g_popup_visible = true;
}

void windows_close_popup() { g_popup_visible = false; }

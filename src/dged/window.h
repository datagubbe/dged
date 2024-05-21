#ifndef _WINDOW_H
#define _WINDOW_H

#include <stdbool.h>
#include <stdint.h>

#include "btree.h"

struct command_list;
struct display;
struct keymap;
struct commands;
struct buffer;
struct buffers;

struct window;
struct windows;

struct window_position {
  uint32_t x;
  uint32_t y;
};

void windows_init(uint32_t height, uint32_t width,
                  struct buffer *initial_buffer, struct buffer *minibuffer,
                  struct buffers *buffers);

void windows_destroy(void);
void windows_resize(uint32_t height, uint32_t width);
void windows_update(void *(*frame_alloc)(size_t), float frame_time);
void windows_render(struct display *display);

struct window *root_window(void);
struct window *minibuffer_window(void);
struct window *popup_window(void);
bool popup_window_visible(void);

void windows_set_active(struct window *window);
struct window *windows_focus(uint32_t id);
struct window *windows_get_active(void);
struct window *windows_focus_next(void);
struct window *window_find_by_buffer(struct buffer *b);

void window_set_buffer(struct window *window, struct buffer *buffer);
void window_set_buffer_e(struct window *window, struct buffer *buffer,
                         bool modeline, bool line_numbers);
struct buffer *window_buffer(struct window *window);
struct buffer_view *window_buffer_view(struct window *window);
struct buffer_view *window_prev_buffer_view(struct window *window);
bool window_has_prev_buffer_view(struct window *window);
uint32_t window_width(const struct window *window);
uint32_t window_height(const struct window *window);
struct window_position window_position(const struct window *window);

void window_close(struct window *window);
void window_close_others(struct window *window);
void window_split(struct window *window, struct window **new_window_a,
                  struct window **new_window_b);
void window_hsplit(struct window *window, struct window **new_window_a,
                   struct window **new_window_b);
void window_vsplit(struct window *window, struct window **new_window_a,
                   struct window **new_window_b);

void windows_show_popup(uint32_t row, uint32_t col, uint32_t width,
                        uint32_t height);
void windows_close_popup(void);

#endif

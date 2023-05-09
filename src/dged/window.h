#include <stdbool.h>
#include <stdint.h>

#include "btree.h"

struct command_list;
struct display;
struct keymap;
struct commands;
struct buffer;

struct window;
struct windows;

void windows_init(uint32_t height, uint32_t width,
                  struct buffer *initial_buffer, struct buffer *minibuffer);

void windows_destroy();
void windows_resize(uint32_t height, uint32_t width);
void windows_update(void *(*frame_alloc)(size_t), uint64_t frame_time);
void windows_render(struct display *display);

struct window *root_window();
struct window *minibuffer_window();

void windows_set_active(struct window *window);
struct window *windows_focus(uint32_t id);
struct window *windows_get_active();
struct window *windows_focus_next();
struct window *window_find_by_buffer(struct buffer *b);

void window_set_buffer(struct window *window, struct buffer *buffer);
struct buffer *window_buffer(struct window *window);
struct buffer_view *window_buffer_view(struct window *window);
struct buffer *window_prev_buffer(struct window *window);
bool window_has_prev_buffer(struct window *window);
struct buffer_location window_cursor_location(struct window *window);
struct buffer_location window_absolute_cursor_location(struct window *window);
uint32_t window_width(struct window *window);
uint32_t window_height(struct window *window);

void window_close(struct window *window);
void window_close_others(struct window *window);
void window_split(struct window *window, struct window **new_window_a,
                  struct window **new_window_b);
void window_hsplit(struct window *window, struct window **new_window_a,
                   struct window **new_window_b);
void window_vsplit(struct window *window, struct window **new_window_a,
                   struct window **new_window_b);

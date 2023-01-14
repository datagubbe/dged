#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <termios.h>

struct display {
  struct termios term;
  struct termios orig_term;
  uint32_t width;
  uint32_t height;
};

struct render_command;
struct command_list;

struct display display_create();
void display_resize(struct display *display);
void display_destroy(struct display *display);

void display_clear(struct display *display);
void display_move_cursor(struct display *display, uint32_t row, uint32_t col);

void display_begin_render(struct display *display);
void display_render(struct display *display, struct command_list *command_list);
void display_end_render(struct display *display);

typedef void *(*alloc_fn)(size_t);
struct command_list *command_list_create(uint32_t capacity, alloc_fn allocator,
                                         uint32_t xoffset, uint32_t yoffset,
                                         const char *name);

void command_list_set_show_whitespace(struct command_list *list, bool show);
void command_list_set_index_color_bg(struct command_list *list,
                                     uint8_t color_idx);
void command_list_set_color_bg(struct command_list *list, uint8_t red,
                               uint8_t green, uint8_t blue);
void command_list_set_index_color_fg(struct command_list *list,
                                     uint8_t color_idx);
void command_list_set_color_fg(struct command_list *list, uint8_t red,
                               uint8_t green, uint8_t blue);
void command_list_reset_color(struct command_list *list);
void command_list_draw_text(struct command_list *list, uint32_t col,
                            uint32_t row, uint8_t *data, uint32_t len);
void command_list_draw_text_copy(struct command_list *list, uint32_t col,
                                 uint32_t row, uint8_t *data, uint32_t len);
void command_list_draw_repeated(struct command_list *list, uint32_t col,
                                uint32_t row, uint8_t c, uint32_t nrepeat);

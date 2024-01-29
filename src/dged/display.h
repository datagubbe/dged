#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct display;

struct render_command;
struct command_list;

/**
 * Create a new display
 *
 * The only implementation of this is currently a termios one.
 * @returns A pointer to the display.
 */
struct display *display_create();

/**
 * Resize the display
 *
 * Resize the display to match the underlying size of the device.
 * @param display The display to resize.
 */
void display_resize(struct display *display);

/**
 * Destroy the display.
 *
 * Clear all resources associated with the display and reset the underlying
 * device to original state.
 * @param display The display to destroy.
 */
void display_destroy(struct display *display);

/**
 * Get the current width of the display.
 *
 * @param display The display to get width for.
 * @returns The width in number of chars as a positive integer.
 */
uint32_t display_width(struct display *display);

/**
 * Get the current height of the display.
 *
 * @param display The display to get height for.
 * @returns The height in number of chars as a positive integer.
 */
uint32_t display_height(struct display *display);

/**
 * Clear the display
 *
 * This will clear all text from the display.
 * @param display The display to clear.
 */
void display_clear(struct display *display);

/**
 * Move the cursor to the specified location
 *
 * Move the cursor to the specified row and column.
 * @param display The display to move the cursor for.
 * @param row The row to move the cursor to.
 * @param col The col to move the cursor to.
 */
void display_move_cursor(struct display *display, uint32_t row, uint32_t col);

/**
 * Start a render pass on the display.
 *
 * A begin_render call can be followed by any number of render calls followed by
 * a single end_render call.
 * @param display The display to begin rendering on.
 */
void display_begin_render(struct display *display);

/**
 * Render a command list on the display.
 *
 * Render a command list on the given display. A command list is a series of
 * rendering instructions.
 * @param display The display to render on.
 * @param command_list The command list to render.
 */
void display_render(struct display *display, struct command_list *command_list);

/**
 * Finish a render pass on the display.
 *
 * This tells the display that rendering is done for now and a flush is
 * triggered to update the display hardware.
 * @param display The display to end rendering on.
 */
void display_end_render(struct display *display);

/**
 * Create a new command list.
 *
 * A command list records a series of commands for drawing text to a display.
 * @param capacity The capacity of the command list.
 * @param allocator Allocation callback to use for data in the command list.
 * @param xoffset Column offset to apply to all operations in the list.
 * @param yoffset Row offset to apply to all operations in the list.
 * @param name Name for the command list. Useful for debugging.
 * @returns A pointer to the created command list.
 */
struct command_list *command_list_create(uint32_t capacity,
                                         void *(*allocator)(size_t),
                                         uint32_t xoffset, uint32_t yoffset,
                                         const char *name);

/**
 * Enable/disable rendering of whitespace characters.
 *
 * ' ' will be rendered with a dot and '\t' as an arrow.
 * @param list Command list to record command in.
 * @param show True if whitespace chars should be displayed, false otherwise.
 */
void command_list_set_show_whitespace(struct command_list *list, bool show);

enum colors {
  Color_Black = 0,
  Color_Red,
  Color_Green,
  Color_Yellow,
  Color_Blue,
  Color_Magenta,
  Color_Cyan,
  Color_White,
  Color_BrightBlack = 8,
  Color_BrightRed,
  Color_BrightGreen,
  Color_BrightYellow,
  Color_BrightBlue,
  Color_BrightMagenta,
  Color_BrightCyan,
  Color_BrightWhite
};

/**
 * Set background color
 *
 * Set the background color to use for following draw commands to the specified
 * index. Note that color indices > 15 might not be supported on all displays.
 * @param list The command list to record command in.
 * @param color_idx The color index to use as background (0-255)
 */
void command_list_set_index_color_bg(struct command_list *list,
                                     uint8_t color_idx);

/**
 * Set background color
 *
 * Set the background color to use for following draw commands to the specified
 * RGB color. Note that this might not be supported on all displays.
 * @param list The command list to record command in.
 * @param red Red value.
 * @param green Green value.
 * @param blue Blue value.
 */
void command_list_set_color_bg(struct command_list *list, uint8_t red,
                               uint8_t green, uint8_t blue);

/**
 * Set foreground color
 *
 * Set the foreground color to use for following draw commands to the specified
 * index. Note that color indices > 15 might not be supported on all displays.
 * @param list The command list to record command in.
 * @param color_idx The color index to use as foreground (0-255)
 */
void command_list_set_index_color_fg(struct command_list *list,
                                     uint8_t color_idx);

/**
 * Set foreground color
 *
 * Set the foreground color to use for following draw commands to the specified
 * RGB color. Note that this might not be supported on all displays.
 * @param list The command list to record command in.
 * @param red Red value.
 * @param green Green value.
 * @param blue Blue value.
 */
void command_list_set_color_fg(struct command_list *list, uint8_t red,
                               uint8_t green, uint8_t blue);

/**
 * Reset the color and styling information.
 *
 * The following draw commands will have their formatting reset to the default.
 * @param list The command list to record command in.
 */
void command_list_reset_color(struct command_list *list);

/**
 * Draw text
 *
 * @param list Command list to record draw command in.
 * @param col Column to start text at.
 * @param row Row to start text at.
 * @param data Bytes to write.
 * @param len Number of bytes to write.
 */
void command_list_draw_text(struct command_list *list, uint32_t col,
                            uint32_t row, uint8_t *data, uint32_t len);

/**
 * Draw text, copying it to internal storage first.
 *
 * @param list Command list to record draw command in.
 * @param col Column to start text at.
 * @param row Row to start text at.
 * @param data Bytes to write.
 * @param len Number of bytes to write.
 */
void command_list_draw_text_copy(struct command_list *list, uint32_t col,
                                 uint32_t row, uint8_t *data, uint32_t len);

/**
 * Draw a repeated character.
 *
 * @param list Command list to record draw command in.
 * @param col Column to start text at.
 * @param row Row to start text at.
 * @param c Character to repeat.
 * @param nrepeat Number of times to repeat byte.
 */
void command_list_draw_repeated(struct command_list *list, uint32_t col,
                                uint32_t row, int32_t c, uint32_t nrepeat);

void command_list_draw_command_list(struct command_list *list,
                                    struct command_list *to_draw);

#ifndef _BUFFER_H
#define _BUFFER_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "command.h"
#include "lang.h"
#include "location.h"
#include "text.h"
#include "undo.h"
#include "window.h"

struct command_list;
struct hooks;

/** @file buffer.h
 * A buffer of text.
 *
 * The buffer is one of the fundamental types of DGED. Most
 * of the text editing operations are implemented here.
 */

/**
 * A buffer of text that can be modified, read from and written to disk.
 *
 * This is the central data structure of dged and most other behavior is
 * implemented on top of it.
 */
struct buffer {

  /** Buffer name */
  char *name;

  /** Associated filename, this is where the buffer will be saved to */
  char *filename;

  /** Time when buffer was last written to disk */
  struct timespec last_write;

  /** Hooks for this buffer */
  struct hooks *hooks;

  /** Text data structure */
  struct text *text;

  /** Buffer undo stack */
  struct undo_stack undo;

  /** Buffer programming language */
  struct language lang;

  /** Has this buffer been modified from when it was last saved */
  bool modified;

  /** Can this buffer be changed */
  bool readonly;

  /** Can rows be added lazily to this buffer */
  bool lazy_row_add;
};

void buffer_static_init();
void buffer_static_teardown();

/**
 * Create a new buffer.
 *
 * @param [in] name The buffer name.
 * @returns A new buffer
 */
struct buffer buffer_create(const char *name);

/**
 * Create a new buffer from a file path.
 *
 * @param [in] path Path to the file to load into the new buffer.
 * @returns A new buffer with @p path loaded.
 */
struct buffer buffer_from_file(const char *path);

/**
 * Save buffer to the backing file.
 *
 * @param [in] buffer Buffer to save.
 */
void buffer_to_file(struct buffer *buffer);

/**
 * Set path to backing file for buffer.
 *
 * The backing file is used when writing the buffer to a file.
 * @param [in] buffer The buffer to set filename for.
 * @param [in] filename The filename to use. It is required that this is a full
 * path.
 */
void buffer_set_filename(struct buffer *buffer, const char *filename);

/**
 * Reload the buffer from disk.
 *
 * Reload the buffer from the backing file.
 * @param [in] buffer The buffer to reload.
 */
void buffer_reload(struct buffer *buffer);

/**
 * Destroy the buffer.
 *
 * Destroy the buffer, releasing all associated resources.
 * @param [in] buffer The buffer to destroy.
 */
void buffer_destroy(struct buffer *buffer);

/**
 * Add text to the buffer at the specified location.
 *
 * @param [in] buffer The buffer to add text to.
 * @param [in] at The location to add text at.
 * @param [in] text Pointer to the text bytes, not NULL-terminated.
 * @param [in] nbytes Number of bytes in @ref text.
 *
 * @returns The location at the end of the inserted text.
 */
struct location buffer_add(struct buffer *buffer, struct location at,
                           uint8_t *text, uint32_t nbytes);

/**
 * Set the entire text contents of the buffer.
 *
 * @param [in] buffer The buffer to set text for.
 * @param [in] text Pointer to the text bytes, not NULL-terminated.
 * @param [in] nbytes Number of bytes in @ref text.
 *
 * @returns The location at the end of the inserted text
 */
struct location buffer_set_text(struct buffer *buffer, uint8_t *text,
                                uint32_t nbytes);

/**
 * Clear all text in the buffer
 *
 * @param [in] buffer The buffer to clear.
 */
void buffer_clear(struct buffer *buffer);

/**
 * Does buffer contain any text?
 *
 * @param [in] buffer The buffer to check.
 * @returns True if the buffer is empty (has no text in it), false otherwise.
 */
bool buffer_is_empty(struct buffer *buffer);

/**
 * Has the buffer been modified since it was last retrieved from/saved to disk?
 *
 * @param [in] buffer The buffer to examine.
 * @returns True if the buffer has been modified, false otherwise.
 */
bool buffer_is_modified(struct buffer *buffer);

/**
 * Is this buffer read-only?
 *
 * @param [in] buffer The buffer to examine.
 * @returns True if the buffer is read-only (cannot be modified), false
 * otherwise.
 */
bool buffer_is_readonly(struct buffer *buffer);

/**
 * Set the read-only status for the buffer.
 *
 * @param [in] buffer The buffer to set read-only for.
 * @param [in] readonly If true, the buffer is set to read-only, otherwise it is
 * set to writable.
 */
void buffer_set_readonly(struct buffer *buffer, bool readonly);

/**
 * Is the buffer backed by a file on disk?
 *
 * @param [in] buffer The buffer to examine.
 * @returns True if the buffer has a path to a file on disk to use as backing
 * file, false otherwise. Note that this function returns true even if the
 * buffer has never been written to the backing file.
 */
bool buffer_is_backed(struct buffer *buffer);

/**
 * Get location of previous character in buffer.
 *
 * @param [in] buffer The buffer to use.
 * @param [in] dot The location to start from.
 * @returns The location in front of the previous char given @p dot.
 */
struct location buffer_previous_char(struct buffer *buffer,
                                     struct location dot);

/**
 * Get location of previous word in buffer.
 *
 * @param [in] buffer The buffer to look in.
 * @param [in] dot The location to start from.
 * @returns The location at the start of the previous word, given @p dot.
 */
struct location buffer_previous_word(struct buffer *buffer,
                                     struct location dot);

/**
 * Get location of previous line.
 *
 * @param [in] buffer The buffer to look in.
 * @param [in] dot The location to start from.
 * @returns The location at the start of the line above the current one (the one
 * @p dot is on). If @p dot is on the first line, the location (0, 0) is
 * returned.
 */
struct location buffer_previous_line(struct buffer *buffer,
                                     struct location dot);

/**
 * Get location of next character in buffer.
 *
 * @param [in] buffer The buffer to use.
 * @param [in] dot The location to start from.
 * @returns The location in front of the next char given @p dot.
 */
struct location buffer_next_char(struct buffer *buffer, struct location dot);

/**
 * Get location of next word in buffer.
 *
 * @param [in] buffer The buffer to look in.
 * @param [in] dot The location to start from.
 * @returns The location at the start of the next word, given @p dot.
 */
struct location buffer_next_word(struct buffer *buffer, struct location dot);

/**
 * Get location of next line.
 *
 * @param [in] buffer The buffer to look in.
 * @param [in] dot The location to start from.
 * @returns The location at the start of the line above the current one (the one
 * @p dot is on). If @p dot is on the last line, the last location in the
 * buffer is returned.
 */
struct location buffer_next_line(struct buffer *buffer, struct location dot);

/**
 * Get the extents of the word located at @p at.
 *
 * @param [in] buffer The buffer to look in.
 * @param [in] at The location to start from.
 *
 * @returns The extent of the closest word as a region. If
 * there is no word, the region will be zero-sized.
 */
struct region buffer_word_at(struct buffer *buffer, struct location at);

/**
 * Clamp a buffer position to the boundaries of the buffer.
 *
 * Note that both @ref line and @p col can be negative or bigger than the
 * buffer.
 *
 * @param [in] buffer The buffer to use for clamping.
 * @param [in] line The line position to clamp.
 * @param [in] col The column position to clamp.
 * @returns The closest position inside the buffer matching (line, col).
 */
struct location buffer_clamp(struct buffer *buffer, int64_t line, int64_t col);

/**
 * Get location of buffer end.
 *
 * @param [in] buffer The buffer to use.
 * @returns the position after the last character in @ref buffer.
 */
struct location buffer_end(struct buffer *buffer);

/**
 * Get the number of lines in the buffer
 *
 * @param [in] buffer The buffer to use.
 * @returns The number of lines in @ref buffer.
 */
uint32_t buffer_num_lines(struct buffer *buffer);

/**
 * Get the number of chars in a given line in buffer.
 *
 * @param [in] buffer The buffer to use.
 * @param [in] line The line to get number of chars for.
 * @returns The number of chars in @ref line.
 */
uint32_t buffer_num_chars(struct buffer *buffer, uint32_t line);

/**
 * Insert a newline in the buffer.
 *
 * @param [in] buffer The buffer to insert in.
 * @param [in] at The point to insert at.
 * @returns The location at the start of the new line.
 */
struct location buffer_newline(struct buffer *buffer, struct location at);

/**
 * Insert indentation in the buffer.
 *
 * @param [in] buffer The buffer to indent in.
 * @param [in] at The location to insert indentation at.
 * @returns The position after indenting.
 */
struct location buffer_indent(struct buffer *buffer, struct location at);

/**
 * Insert alternative indentation in the buffer.
 *
 * Alternative indentation is spaces if it is normally using tabs
 * and vice versa.
 *
 * @param [in] buffer The buffer to indent in.
 * @param [in] at The location to insert indentation at.
 * @returns The position after indenting.
 */
struct location buffer_indent_alt(struct buffer *buffer, struct location at);

/**
 * Undo the last operation in the buffer.
 *
 * @param [in] buffer The buffer to undo in.
 * @param [in] dot The point to undo at.
 * @returns The position in the buffer after undo.
 */
struct location buffer_undo(struct buffer *buffer, struct location dot);

/**
 * Search for a substring in the buffer.
 *
 * @param [in] buffer The buffer to search in.
 * @param [in] pattern The substring to search for.
 * @param [out] matches The pointer passed in is modified to point at the
 * resulting matches. This pointer should be freed using @c free.
 * @param [nmatches] nmatches The pointer passed in is modified to point at the
 * number of resulting matches.
 */
void buffer_find(struct buffer *buffer, const char *pattern,
                 struct region **matches, uint32_t *nmatches);

/**
 * Copy a region in the buffer into the kill ring.
 *
 * @param [in] buffer The buffer to copy from.
 * @param [in] region The region to copy.
 * @returns The position in the buffer after the copy.
 */
struct location buffer_copy(struct buffer *buffer, struct region region);

/**
 * Cut a region in the buffer into the kill ring.
 *
 * @param [in] buffer The buffer to cut from.
 * @param [in] region The region to cut.
 * @returns The position in the buffer after the cut.
 */
struct location buffer_cut(struct buffer *buffer, struct region region);

/**
 * Delete a region in the buffer without putting it into the kill ring.
 *
 * @param [in] buffer The buffer to delete from.
 * @param [in] region The region to delete.
 * @returns The position in the buffer after the delete.
 */
struct location buffer_delete(struct buffer *buffer, struct region region);

/**
 * Paste from the kill ring into the buffer.
 *
 * @param [in] buffer Buffer to paste in.
 * @param [in] at Location to paste at.
 * @returns The location in the buffer after the paste.
 */
struct location buffer_paste(struct buffer *buffer, struct location at);

/**
 * Paste the next older entry from the kill ring into the buffer.
 *
 * @param [in] buffer Buffer to paste in.
 * @param [in] at Location to paste at.
 * @returns The location in the buffer after the paste.
 */
struct location buffer_paste_older(struct buffer *buffer, struct location at);

/**
 * Get one line from the buffer.
 *
 * @param buffer The buffer to get the line from.
 * @param line Line number in the buffer.
 * @returns A text chunk describing the line. Note that if the line number is
 * greater than the number of lines, the @ref text_chunk will be empty.
 */
struct text_chunk buffer_line(struct buffer *buffer, uint32_t line);

/**
 * Get a region of text from the buffer.
 *
 * @param buffer The buffer to get text from.
 * @param region A region representing the buffer area to get text from.
 *
 * @returns A text chunk describing the region.
 */
struct text_chunk buffer_region(struct buffer *buffer, struct region region);

/**
 * Add a text property to a region of the buffer.
 *
 * @param buffer The buffer to add a text property to.
 * @param start The start of the region to set the property for.
 * @param end The end of the region to set the property for.
 * @param property The text property to set.
 */
void buffer_add_text_property(struct buffer *buffer, struct location start,
                              struct location end,
                              struct text_property property);

/**
 * Get active text properties at @p location in @p buffer.
 *
 * @param buffer The buffer to get properties for.
 * @param location The location to get properties at.
 * @param properties Caller-provided array of properties set by this function.
 * @param max_nproperties Max num properties to put in @p properties.
 * @param nproperties Number of properties that got stored in @p properties.
 */
void buffer_get_text_properties(struct buffer *buffer, struct location location,
                                struct text_property **properties,
                                uint32_t max_nproperties,
                                uint32_t *nproperties);

/**
 * Clear any text properties for @p buffer.
 *
 * @param buffer The buffer to clear properties for.
 */
void buffer_clear_text_properties(struct buffer *buffer);

/** Callback when removing hooks to clean up userdata */
typedef void (*remove_hook_cb)(void *userdata);

/**
 * Buffer update hook callback function.
 *
 * @param buffer The buffer.
 * @param userdata Userdata pointer passed in to @ref buffer_add_update_hook.
 */
typedef void (*update_hook_cb)(struct buffer *buffer, void *userdata);

/**
 * Add a buffer update hook.
 *
 * @param [in] buffer The buffer to add the hook to.
 * @param [in] hook The update hook callback.
 * @param [in] userdata Data that is passed unmodified to the update hook.
 * @returns The hook id.
 */
uint32_t buffer_add_update_hook(struct buffer *buffer, update_hook_cb hook,
                                void *userdata);

/**
 * Remove a buffer update hook.
 *
 * @param [in] buffer The buffer to remove the hook from.
 * @param [in] hook_id The hook id as returned from @ref buffer_add_update_hook.
 * @param [in] callback A function called with the userdata pointer to do
 * cleanup.
 */
void buffer_remove_update_hook(struct buffer *buffer, uint32_t hook_id,
                               remove_hook_cb callback);

/**
 * Buffer render hook callback function.
 *
 * @param buffer The buffer.
 * @param userdata Userdata sent in when registering the hook.
 * @param origin The upper left corner of the region of the buffer
 *   currently rendering.
 * @param width The width of the rendered region.
 * @param height The height of the rendered region.
 */
typedef void (*render_hook_cb)(struct buffer *buffer, void *userdata,
                               struct location origin, uint32_t width,
                               uint32_t height);

/**
 * Add a buffer render hook.
 *
 * @param [in] buffer The buffer to add the hook to.
 * @param [in] callback The render hook callback.
 * @param [in] userdata Data that is passed unmodified to the render hook.
 * @returns The hook id.
 */
uint32_t buffer_add_render_hook(struct buffer *buffer, render_hook_cb callback,
                                void *userdata);

/**
 * Remove a buffer render hook.
 *
 * @param [in] buffer The buffer to remove the hook from.
 * @param [in] hook_id The hook id as returned from @ref buffer_add_render_hook.
 * @param [in] callback A function called with the userdata pointer to do
 * cleanup.
 */
void buffer_remove_render_hook(struct buffer *buffer, uint32_t hook_id,
                               remove_hook_cb callback);

/**
 * Buffer reload hook callback function.
 *
 * @param buffer The buffer.
 * @param userdata The userdata as sent in to @ref buffer_add_reload_hook.
 */
typedef void (*reload_hook_cb)(struct buffer *buffer, void *userdata);

/**
 * Add a reload hook, called when the buffer is reloaded from disk.
 *
 * Since this is called when the buffer is reloaded from disk it is
 * only useful for buffers that are backed by a file on disk.
 *
 * @param buffer The buffer to add a reload hook to.
 * @param callback The function to call when @p buffer is reloaded.
 * @param userdata Data that is passed unmodified to the reload hook.
 * @returns The hook id.
 */
uint32_t buffer_add_reload_hook(struct buffer *buffer, reload_hook_cb callback,
                                void *userdata);

/**
 * Remove a buffer reload hook.
 *
 * @param [in] buffer The buffer to remove the hook from.
 * @param [in] hook_id The hook id as returned from @ref buffer_add_reload_hook.
 * @param [in] callback A function called with the userdata pointer to do
 * cleanup.
 */
void buffer_remove_reload_hook(struct buffer *buffer, uint32_t hook_id,
                               remove_hook_cb callback);

/**
 * Buffer insert hook callback function.
 *
 * @param buffer The buffer.
 * @param inserted The position in the @p buffer where text was inserted.
 * @param begin_idx The global byte offset to the start of where text was
 * inserted.
 * @param end_idx The global byte offset to the end of where text was inserted.
 * @param userdata The userdata as sent in to @ref buffer_add_insert_hook.
 */
typedef void (*insert_hook_cb)(struct buffer *buffer, struct region inserted,
                               uint32_t begin_idx, uint32_t end_idx,
                               void *userdata);

/**
 * Add an insert hook, called when text is inserted into the @p buffer.
 *
 * @param buffer The buffer to add an insert hook to.
 * @param callback The function to call when text is inserted into @p buffer.
 * @param userdata Data that is passed unmodified to the insert hook.
 * @returns The hook id.
 */
uint32_t buffer_add_insert_hook(struct buffer *buffer, insert_hook_cb callback,
                                void *userdata);

/**
 * Remove a buffer insert hook.
 *
 * @param [in] buffer The buffer to remove the hook from.
 * @param [in] hook_id The hook id as returned from @ref buffer_add_insert_hook.
 * @param [in] callback A function called with the userdata pointer to do
 * cleanup.
 */
void buffer_remove_insert_hook(struct buffer *buffer, uint32_t hook_id,
                               remove_hook_cb callback);

/**
 * Buffer delete hook callback function
 *
 * @param buffer The buffer.
 * @param removed The region that was removed from the @p buffer.
 * @param begin_idx The global byte offset to the start of the removed text.
 * @param end_idx The global byte offset to the end of the removed text.
 * @param userdata The userdata as sent in to @ref buffer_add_delete_hook.
 */
typedef void (*delete_hook_cb)(struct buffer *buffer, struct region removed,
                               uint32_t begin_idx, uint32_t end_idx,
                               void *userdata);

/**
 * Add a delete hook, called when text is removed from the @p buffer.
 *
 * @param buffer The buffer to add a delete hook to.
 * @param callback The function to call when text is removed from @p buffer.
 * @param userdata Data that is passed unmodified to the delete hook.
 * @returns The hook id.
 */
uint32_t buffer_add_delete_hook(struct buffer *buffer, delete_hook_cb callback,
                                void *userdata);

/**
 * Remove a buffer delete hook.
 *
 * @param [in] buffer The buffer to remove the hook from.
 * @param [in] hook_id The hook id as returned from @ref buffer_add_delete_hook.
 * @param [in] callback A function called with the userdata pointer to do
 * cleanup.
 */
void buffer_remove_delete_hook(struct buffer *buffer, uint32_t hook_id,
                               remove_hook_cb callback);

/**
 * Buffer destroy hook callback function.
 *
 * @param buffer The buffer.
 * @param userdata The userdata as sent in to @ref buffer_add_destroy_hook.
 */
typedef void (*destroy_hook_cb)(struct buffer *buffer, void *userdata);

/**
 * Add a destroy hook, called when @p buffer is destroyed.
 *
 * @param buffer The buffer to add a destroy hook to.
 * @param callback The function to call @p buffer is destroyed.
 * @param userdata Data that is passed unmodified to the destroy hook.
 * @returns The hook id.
 */
uint32_t buffer_add_destroy_hook(struct buffer *buffer,
                                 destroy_hook_cb callback, void *userdata);

/**
 * Remove a buffer destroy hook.
 *
 * @param [in] buffer The buffer to remove the hook from.
 * @param [in] hook_id The hook id as returned from @ref
 * buffer_add_destroy_hook.
 * @param [in] callback A function called with the userdata pointer to do
 * cleanup.
 */
void buffer_remove_destroy_hook(struct buffer *buffer, uint32_t hook_id,
                                remove_hook_cb callback);

/**
 * Buffer create hook callback function.
 *
 * @param buffer The newly created buffer.
 * @param userdata The userdata as sent in to @ref buffer_add_create_hook.
 */
typedef void (*create_hook_cb)(struct buffer *buffer, void *userdata);

/**
 * Add a buffer create hook, called everytime a new buffer is created.
 *
 * @param [in] callback Create hook callback.
 * @param [in] userdata Pointer to data that is passed unmodified to the update
 * hook.
 * @returns The hook id.
 */
uint32_t buffer_add_create_hook(create_hook_cb callback, void *userdata);

/**
 * Remove a buffer create hook.
 *
 * @param [in] hook_id The hook id as returned from @ref buffer_add_create_hook.
 * @param [in] callback A function called with the userdata pointer to do
 * cleanup.
 */
void buffer_remove_create_hook(uint32_t hook_id, remove_hook_cb callback);

/**
 * Parameters for updating a buffer.
 */
struct buffer_update_params {};

/**
 * Parameters for rendering a buffer.
 */
struct buffer_render_params {

  /** Command list to add rendering commands for the buffer */
  struct command_list *commands;

  /** Where is the upper left corner of the buffer */
  struct location origin;

  /** Window width for this buffer, -1 if it is not in a window */
  uint32_t width;

  /** Window height for this buffer, -1 if it is not in a window */
  uint32_t height;
};

/**
 * Update a buffer.
 *
 * @param [in] buffer The buffer to update.
 * @param [inout] params The parameters for the update. The @ref commands field
 * in @p params will be modified with the rendering commands needed for this
 * buffer.
 */
void buffer_update(struct buffer *buffer, struct buffer_update_params *params);

/**
 * Render a buffer.
 * @param [in] buffer The buffer to render.
 * @param [inout] params The parameters for the rendering.
 */
void buffer_render(struct buffer *buffer, struct buffer_render_params *params);

// TODO: move this to where it makes sense
uint32_t visual_string_width(uint8_t *txt, uint32_t len, uint32_t start_col,
                             uint32_t end_col);

/**
 * Sort lines in a buffer alphabetically.
 *
 * @param [in] buffer The buffer to sort lines in.
 * @param [in] start_line The first line to sort.
 * @param [in] end_line The last line to sort.
 */
void buffer_sort_lines(struct buffer *buffer, uint32_t start_line,
                       uint32_t end_line);

#endif

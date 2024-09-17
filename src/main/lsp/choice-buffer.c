#include "choice-buffer.h"

#include "dged/binding.h"
#include "dged/buffer.h"
#include "dged/buffer_view.h"
#include "dged/buffers.h"
#include "dged/command.h"
#include "dged/display.h"
#include "dged/location.h"

#include "main/bindings.h"

struct choice {
  struct region region;
  void *data;
  select_callback callback;
};

struct choice_buffer {
  struct buffers *buffers;
  struct buffer *buffer;
  VEC(struct choice) choices;

  abort_callback abort_cb;
  select_callback select_cb;
  update_callback update_cb;
  void *userdata;

  uint32_t buffer_removed_hook;

  struct command enter_pressed;
  struct command q_pressed;
};

static void delete_choice_buffer(struct choice_buffer *buffer,
                                 bool delete_underlying);

static void underlying_buffer_destroyed(struct buffer *buffer,
                                        void *choice_buffer) {
  (void)buffer;
  struct choice_buffer *cb = (struct choice_buffer *)choice_buffer;

  // run this with false since the underlying buffer is already
  // being deleted
  delete_choice_buffer(cb, false);
}

static int32_t enter_pressed_fn(struct command_ctx ctx, int argc,
                                const char **argv) {
  (void)argc;
  (void)argv;
  struct choice_buffer *cb = (struct choice_buffer *)ctx.userdata;
  struct window *w = window_find_by_buffer(cb->buffer);
  if (w == NULL) {
    return 0;
  }

  struct buffer_view *bv = window_buffer_view(w);

  VEC_FOR_EACH(&cb->choices, struct choice * choice) {
    if (location_is_between(bv->dot, choice->region.begin,
                            choice->region.end)) {
      if (choice->callback != NULL) {
        choice->callback(choice->data, cb->userdata);
      } else {
        cb->select_cb(choice->data, cb->userdata);
      }

      delete_choice_buffer(cb, true);
      return 0;
    }
  }

  return 0;
}

static int32_t choice_buffer_close_fn(struct command_ctx ctx, int argc,
                                      const char **argv) {
  (void)argc;
  (void)argv;

  struct choice_buffer *cb = (struct choice_buffer *)ctx.userdata;
  delete_choice_buffer(cb, true);
  return 0;
}

struct choice_buffer *
choice_buffer_create(struct s8 title, struct buffers *buffers,
                     select_callback selected, abort_callback aborted,
                     update_callback update, void *userdata) {

  struct choice_buffer *b = calloc(1, sizeof(struct choice_buffer));
  VEC_INIT(&b->choices, 16);
  b->select_cb = selected;
  b->abort_cb = aborted;
  b->update_cb = update;
  b->userdata = userdata;
  b->buffers = buffers;

  // set up
  struct buffer buf = buffer_create("*something-choices*");
  buf.lazy_row_add = false;
  buf.retain_properties = true;
  b->buffer = buffers_add(b->buffers, buf);
  // TODO: error?
  b->buffer_removed_hook =
      buffer_add_destroy_hook(b->buffer, underlying_buffer_destroyed, b);

  b->enter_pressed = (struct command){
      .name = "choice-buffer-enter",
      .fn = enter_pressed_fn,
      .userdata = b,
  };

  b->q_pressed = (struct command){
      .name = "choice-buffer-close",
      .fn = choice_buffer_close_fn,
      .userdata = b,
  };

  struct binding bindings[] = {
      ANONYMOUS_BINDING(ENTER, &b->enter_pressed),
      ANONYMOUS_BINDING(None, 'q', &b->q_pressed),
  };

  struct keymap km = keymap_create("choice_buffer", 8);
  keymap_bind_keys(&km, bindings, sizeof(bindings) / sizeof(bindings[0]));
  buffer_add_keymap(b->buffer, km);

  struct location begin = buffer_end(b->buffer);
  buffer_add(b->buffer, buffer_end(b->buffer), title.s, title.l);
  buffer_newline(b->buffer, buffer_end(b->buffer));
  buffer_add(b->buffer, buffer_end(b->buffer), (uint8_t *)"----------------",
             16);
  struct location end = buffer_end(b->buffer);
  buffer_add_text_property(b->buffer, begin, end,
                           (struct text_property){
                               .type = TextProperty_Colors,
                               .data.colors =
                                   (struct text_property_colors){
                                       .set_fg = true,
                                       .fg = Color_Cyan,
                                   },
                           });
  buffer_newline(b->buffer, buffer_end(b->buffer));
  buffer_newline(b->buffer, buffer_end(b->buffer));

  struct window *w = windows_get_active();

  window_set_buffer(w, b->buffer);
  struct buffer_view *bv = window_buffer_view(w);
  bv->dot = buffer_end(b->buffer);

  buffer_set_readonly(b->buffer, true);

  return b;
}

void choice_buffer_add_choice(struct choice_buffer *buffer, struct s8 text,
                              void *data) {
  buffer_set_readonly(buffer->buffer, false);
  VEC_APPEND(&buffer->choices, struct choice * new_choice);

  new_choice->data = data;
  new_choice->callback = NULL;
  new_choice->region.begin = buffer_end(buffer->buffer);
  buffer_add(buffer->buffer, buffer_end(buffer->buffer), (uint8_t *)"- ", 2);
  buffer_add(buffer->buffer, buffer_end(buffer->buffer), text.s, text.l);
  new_choice->region.end = buffer_end(buffer->buffer);
  buffer_newline(buffer->buffer, buffer_end(buffer->buffer));
  buffer_set_readonly(buffer->buffer, false);
}

void choice_buffer_add_choice_with_callback(struct choice_buffer *buffer,
                                            struct s8 text, void *data,
                                            select_callback callback) {
  buffer_set_readonly(buffer->buffer, false);
  VEC_APPEND(&buffer->choices, struct choice * new_choice);

  new_choice->data = data;
  new_choice->callback = callback;
  new_choice->region.begin = buffer_end(buffer->buffer);
  buffer_add(buffer->buffer, buffer_end(buffer->buffer), (uint8_t *)"- ", 2);
  buffer_add(buffer->buffer, buffer_end(buffer->buffer), text.s, text.l);
  new_choice->region.end = buffer_end(buffer->buffer);
  buffer_newline(buffer->buffer, buffer_end(buffer->buffer));
  buffer_set_readonly(buffer->buffer, false);
}

static void delete_choice_buffer(struct choice_buffer *buffer,
                                 bool delete_underlying) {
  buffer->abort_cb(buffer->userdata);
  VEC_DESTROY(&buffer->choices);
  if (delete_underlying) {
    buffer_remove_destroy_hook(buffer->buffer, buffer->buffer_removed_hook,
                               NULL);
    buffers_remove(buffer->buffers, buffer->buffer->name);
  }

  free(buffer);
}

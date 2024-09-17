#include "goto.h"

#include "dged/binding.h"
#include "dged/buffer.h"
#include "dged/buffer_view.h"
#include "dged/buffers.h"
#include "dged/location.h"
#include "dged/minibuffer.h"
#include "dged/window.h"
#include "main/bindings.h"
#include "main/lsp.h"

#include "choice-buffer.h"

static struct jump_stack {
  buffer_keymap_id goto_keymap_id;
  struct buffer_location *stack;
  uint32_t top;
  uint32_t size;
  struct buffers *buffers;
} g_jump_stack;

static struct location_result g_location_result = {};

struct buffer_location {
  struct buffer *buffer;
  struct location location;
};

void init_goto(size_t jump_stack_depth, struct buffers *buffers) {
  g_jump_stack.size = jump_stack_depth;
  g_jump_stack.top = 0;
  g_jump_stack.goto_keymap_id = (buffer_keymap_id)-1;
  g_jump_stack.stack = calloc(g_jump_stack.size, sizeof(struct jump_stack));
  g_jump_stack.buffers = buffers;
}

void destroy_goto(void) {
  free(g_jump_stack.stack);
  g_jump_stack.stack = NULL;
  g_jump_stack.top = 0;
  g_jump_stack.size = 0;
}

void lsp_jump_to(struct text_document_location loc) {
  if (s8startswith(loc.uri, s8("file://"))) {
    const char *p = s8tocstr(loc.uri);
    struct buffer *b = buffers_find_by_filename(g_jump_stack.buffers, &p[7]);

    if (b == NULL) {
      struct buffer new_buf = buffer_from_file(&p[7]);
      b = buffers_add(g_jump_stack.buffers, new_buf);
    }

    free((void *)p);

    struct window *w = windows_get_active();

    struct buffer_view *old_bv = window_buffer_view(w);
    g_jump_stack.stack[g_jump_stack.top] = (struct buffer_location){
        .buffer = old_bv->buffer,
        .location = old_bv->dot,
    };
    g_jump_stack.top = (g_jump_stack.top + 1) % g_jump_stack.size;

    if (old_bv->buffer != b) {
      struct window *tw = window_find_by_buffer(b);
      if (tw == NULL) {
        window_set_buffer(w, b);
      } else {
        w = tw;
        windows_set_active(w);
      }
    }

    struct buffer_view *bv = window_buffer_view(w);
    buffer_view_goto(bv, loc.range.begin);
  } else {
    message("warning: unsupported LSP URI: %.*s", loc.uri.l, loc.uri.s);
  }
}

static void location_selected(void *location, void *userdata) {
  (void)userdata;
  struct text_document_location *loc =
      (struct text_document_location *)location;
  lsp_jump_to(*loc);
}

static void location_buffer_close(void *userdata) {
  (void)userdata;
  location_result_free(&g_location_result);
}

static void handle_location_result(struct lsp_server *server,
                                   struct lsp_response *response,
                                   void *userdata) {
  struct s8 title = s8((const char *)userdata);
  struct location_result res =
      location_result_from_json(&response->value.result);

  if (res.type == Location_Null ||
      (res.type == Location_Array && VEC_EMPTY(&res.location.array))) {
    minibuffer_echo_timeout(2, "nothing found");
    location_result_free(&res);
    return;
  }

  if (res.type == Location_Single) {
    lsp_jump_to(res.location.single);
    location_result_free(&res);
  } else if (res.type == Location_Array && VEC_SIZE(&res.location.array) == 1) {
    lsp_jump_to(*VEC_FRONT(&res.location.array));
    location_result_free(&res);
  } else if (res.type == Location_Array) {

    g_location_result = res;
    struct choice_buffer *buf =
        choice_buffer_create(title, g_jump_stack.buffers, location_selected,
                             location_buffer_close, NULL, server);

    VEC_FOR_EACH(&res.location.array, struct text_document_location * loc) {
      choice_buffer_add_choice(buf,
                               s8from_fmt("%.*s: %d, %d", loc->uri.l,
                                          loc->uri.s, loc->range.begin.line,
                                          loc->range.begin.col),
                               loc);
    }
  }
}

int32_t lsp_goto_def_cmd(struct command_ctx ctx, int argc, const char **argv) {
  (void)argc;
  (void)argv;

  struct buffer_view *bv = window_buffer_view(ctx.active_window);
  struct buffer *b = bv->buffer;
  struct lsp_server *server = lsp_server_for_buffer(b);

  if (server == NULL) {
    minibuffer_echo_timeout(2, "buffer %s does not have lsp enabled", b->name);
    return 0;
  }

  uint64_t id = new_pending_request(server, handle_location_result,
                                    (void *)"Definitions");
  struct versioned_text_document_identifier doc =
      versioned_identifier_from_buffer(b);
  struct text_document_position params = {
      .uri = doc.uri,
      .position = bv->dot,
  };

  struct s8 json_payload = document_position_to_json(&params);
  lsp_send(lsp_backend(server),
           lsp_create_request(id, s8("textDocument/definition"), json_payload));

  versioned_text_document_identifier_free(&doc);
  s8delete(json_payload);

  return 0;
}

int32_t lsp_goto_decl_cmd(struct command_ctx ctx, int argc, const char **argv) {
  (void)argc;
  (void)argv;

  struct buffer_view *bv = window_buffer_view(ctx.active_window);
  struct buffer *b = bv->buffer;
  struct lsp_server *server = lsp_server_for_buffer(b);

  if (server == NULL) {
    minibuffer_echo_timeout(2, "buffer %s does not have lsp enabled", b->name);
    return 0;
  }

  uint64_t id = new_pending_request(server, handle_location_result,
                                    (void *)"Declarations");
  struct versioned_text_document_identifier doc =
      versioned_identifier_from_buffer(b);
  struct text_document_position params = {
      .uri = doc.uri,
      .position = bv->dot,
  };

  struct s8 json_payload = document_position_to_json(&params);
  lsp_send(
      lsp_backend(server),
      lsp_create_request(id, s8("textDocument/declaration"), json_payload));

  versioned_text_document_identifier_free(&doc);
  s8delete(json_payload);

  return 0;
}

int32_t lsp_goto_impl_cmd(struct command_ctx ctx, int argc, const char **argv) {
  (void)argc;
  (void)argv;

  struct buffer_view *bv = window_buffer_view(ctx.active_window);
  struct buffer *b = bv->buffer;
  struct lsp_server *server = lsp_server_for_buffer(b);

  if (server == NULL) {
    minibuffer_echo_timeout(2, "buffer %s does not have lsp enabled", b->name);
    return 0;
  }

  uint64_t id = new_pending_request(server, handle_location_result,
                                    (void *)"Implementations");
  struct versioned_text_document_identifier doc =
      versioned_identifier_from_buffer(b);
  struct text_document_position params = {
      .uri = doc.uri,
      .position = bv->dot,
  };

  struct s8 json_payload = document_position_to_json(&params);
  lsp_send(
      lsp_backend(server),
      lsp_create_request(id, s8("textDocument/implementation"), json_payload));

  versioned_text_document_identifier_free(&doc);
  s8delete(json_payload);

  return 0;
}

static int32_t handle_lsp_goto_key(struct command_ctx ctx, int argc,
                                   const char **argv) {
  (void)argc;
  (void)argv;

  buffer_remove_keymap(g_jump_stack.goto_keymap_id);
  minibuffer_abort_prompt();

  struct command *cmd = lookup_command(ctx.commands, (char *)ctx.userdata);
  if (cmd == NULL) {
    return 0;
  }

  return execute_command(cmd, ctx.commands, windows_get_active(), ctx.buffers,
                         0, NULL);
}

COMMAND_FN("lsp-goto-definition", goto_d_pressed, handle_lsp_goto_key,
           "lsp-goto-definition");
COMMAND_FN("lsp-goto-declaration", goto_f_pressed, handle_lsp_goto_key,
           "lsp-goto-declaration");

int32_t lsp_goto_cmd(struct command_ctx ctx, int argc, const char **argv) {
  (void)argc;
  (void)argv;

  struct binding bindings[] = {
      ANONYMOUS_BINDING(None, 'd', &goto_d_pressed_command),
      ANONYMOUS_BINDING(None, 'f', &goto_f_pressed_command),
  };
  struct keymap m = keymap_create("lsp-goto", 8);
  keymap_bind_keys(&m, bindings, sizeof(bindings) / sizeof(bindings[0]));
  g_jump_stack.goto_keymap_id = buffer_add_keymap(minibuffer_buffer(), m);
  return minibuffer_keymap_prompt(ctx, "lsp-goto: ", &m);
}

int32_t lsp_goto_previous_cmd(struct command_ctx ctx, int argc,
                              const char **argv) {
  (void)ctx;
  (void)argc;
  (void)argv;

  uint32_t index =
      g_jump_stack.top == 0 ? g_jump_stack.size - 1 : g_jump_stack.top - 1;

  struct buffer_location *loc = &g_jump_stack.stack[index];
  if (loc->buffer == NULL) {
    return 0;
  }

  struct window *w = windows_get_active();
  if (window_buffer(w) != loc->buffer) {
    struct window *tw = window_find_by_buffer(loc->buffer);
    if (tw == NULL) {
      window_set_buffer(w, loc->buffer);
    } else {
      w = tw;
      windows_set_active(w);
    }
  }

  buffer_view_goto(window_buffer_view(w), loc->location);

  loc->buffer = NULL;
  g_jump_stack.top = index;

  return 0;
}

#include "help.h"

#include "dged/binding.h"
#include "dged/buffer.h"
#include "dged/buffer_view.h"
#include "dged/buffers.h"
#include "dged/minibuffer.h"
#include "dged/s8.h"
#include "dged/window.h"

#include "bindings.h"
#include "lsp.h"

static int32_t close_help(struct command_ctx ctx, int argc, const char **argv) {
  (void)argc;
  (void)argv;

  if (window_has_prev_buffer_view(ctx.active_window)) {
    window_set_buffer(ctx.active_window,
                      window_prev_buffer_view(ctx.active_window)->buffer);
  } else {
    minibuffer_echo_timeout(4, "no previous buffer to go to");
  }

  return 0;
}

static void handle_help_response(struct lsp_server *server,
                                 struct lsp_response *response,
                                 void *userdata) {
  (void)server;

  struct buffers *buffers = (struct buffers *)userdata;
  if (response->value.result.type == Json_Null) {
    minibuffer_echo_timeout(4, "help: no help found");
    return;
  }

  struct buffer *b = buffers_find(buffers, "*lsp-help*");
  if (b == NULL) {
    b = buffers_add(buffers, buffer_create("*lsp-help*"));
    static struct command help_close = {
        .name = "help_close",
        .fn = close_help,
    };

    struct binding bindings[] = {
        ANONYMOUS_BINDING(None, 'q', &help_close),
    };
    struct keymap km = keymap_create("help", 2);
    keymap_bind_keys(&km, bindings, sizeof(bindings) / sizeof(bindings[0]));
    buffer_add_keymap(b, km);
  }

  struct hover help = hover_from_json(&response->value.result);

  buffer_set_readonly(b, false);
  buffer_clear(b);
  buffer_add(b, buffer_end(b), help.contents.s, help.contents.l);
  buffer_set_readonly(b, true);

  if (window_find_by_buffer(b) == NULL) {
    window_set_buffer(windows_get_active(), b);
  }
  hover_free(&help);
}

void lsp_help(struct lsp_server *server, struct buffer *buffer,
              struct location at, struct buffers *buffers) {
  uint64_t id = new_pending_request(server, handle_help_response, buffers);
  struct versioned_text_document_identifier doc =
      versioned_identifier_from_buffer(buffer);

  struct text_document_position pos = {
      .uri = doc.uri,
      .position = at,
  };

  struct s8 json_payload = document_position_to_json(&pos);
  lsp_send(lsp_backend(server),
           lsp_create_request(id, s8("textDocument/hover"), json_payload));

  versioned_text_document_identifier_free(&doc);
  s8delete(json_payload);
}

int32_t lsp_help_cmd(struct command_ctx ctx, int argc, const char **argv) {
  (void)argc;
  (void)argv;

  struct buffer_view *bv = window_buffer_view(ctx.active_window);
  struct lsp_server *server = lsp_server_for_lang_id(bv->buffer->lang.id);
  if (server == NULL) {
    minibuffer_echo_timeout(4, "no lsp server associated with %s",
                            bv->buffer->name);
    return 0;
  }

  lsp_help(server, bv->buffer, bv->dot, ctx.buffers);
  return 0;
}

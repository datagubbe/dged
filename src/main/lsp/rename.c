#include "rename.h"

#include "dged/buffer.h"
#include "dged/buffer_view.h"
#include "dged/minibuffer.h"
#include "dged/window.h"

#include "lsp.h"

static void handle_rename_response(struct lsp_server *server,
                                   struct lsp_response *response,
                                   void *userdata) {
  (void)userdata;
  if (response->value.result.type == Json_Null) {
    minibuffer_echo_timeout(4, "rename: no edits");
    return;
  }

  struct workspace_edit edit =
      workspace_edit_from_json(&response->value.result);
  apply_edits(server, &edit);
  workspace_edit_free(&edit);
}

void lsp_rename(struct lsp_server *server, struct buffer *buffer,
                struct location location, struct s8 new_name) {
  uint64_t id = new_pending_request(server, handle_rename_response, NULL);
  struct versioned_text_document_identifier doc =
      versioned_identifier_from_buffer(buffer);

  struct rename_params params = {
      .position.uri = doc.uri,
      .position.position = location,
      .new_name = new_name,
  };

  struct s8 json_payload = rename_params_to_json(&params);
  lsp_send(lsp_backend(server),
           lsp_create_request(id, s8("textDocument/rename"), json_payload));

  versioned_text_document_identifier_free(&doc);
  s8delete(json_payload);
}

int32_t lsp_rename_cmd(struct command_ctx ctx, int argc, const char **argv) {
  if (argc == 0) {
    return minibuffer_prompt(ctx, "rename to: ");
  }

  struct buffer_view *bv = window_buffer_view(ctx.active_window);
  struct lsp_server *server = lsp_server_for_lang_id(bv->buffer->lang.id);
  if (!lsp_server_active(server)) {
    minibuffer_echo_timeout(4, "no working lsp server associated with %s",
                            bv->buffer->name);
    return 0;
  }

  lsp_rename(server, bv->buffer, bv->dot, s8(argv[0]));

  return 0;
}

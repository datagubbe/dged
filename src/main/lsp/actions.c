#include "actions.h"

#include "dged/buffer.h"
#include "dged/buffer_view.h"
#include "dged/lsp.h"
#include "dged/minibuffer.h"
#include "dged/window.h"
#include "main/lsp.h"
#include "main/lsp/diagnostics.h"

#include "choice-buffer.h"
#include "types.h"

static struct code_actions g_code_actions_result = {};

static void code_action_command_selected(void *selected, void *userdata) {
  struct lsp_server *server = (struct lsp_server *)userdata;
  struct lsp_command *command = (struct lsp_command *)selected;
  struct s8 json_payload = lsp_command_to_json(command);

  uint64_t id = new_pending_request(server, NULL, NULL);
  lsp_send(
      lsp_backend(server),
      lsp_create_request(id, s8("workspace/executeCommand"), json_payload));

  s8delete(json_payload);
}

static void code_action_selected(void *selected, void *userdata) {
  struct lsp_server *server = (struct lsp_server *)userdata;
  struct code_action *action = (struct code_action *)selected;

  if (action->has_edit) {
    apply_edits(server, &action->edit);
  }

  if (action->has_command) {
    struct s8 json_payload = lsp_command_to_json(&action->command);

    uint64_t id = new_pending_request(server, NULL, NULL);
    lsp_send(
        lsp_backend(server),
        lsp_create_request(id, s8("workspace/executeCommand"), json_payload));
    s8delete(json_payload);
  }
}

static void code_action_closed(void *userdata) {
  (void)userdata;
  lsp_code_actions_free(&g_code_actions_result);
}

static void handle_code_actions_response(struct lsp_server *server,
                                         struct lsp_response *response,
                                         void *userdata) {
  struct code_actions actions =
      lsp_code_actions_from_json(&response->value.result);

  struct buffers *buffers = (struct buffers *)userdata;

  if (VEC_SIZE(&actions.commands) == 0 &&
      VEC_SIZE(&actions.code_actions) == 0) {
    minibuffer_echo_timeout(4, "no code actions available");
    lsp_code_actions_free(&actions);
  } else {
    g_code_actions_result = actions;
    struct choice_buffer *buf =
        choice_buffer_create(s8("Code Actions"), buffers, code_action_selected,
                             code_action_closed, NULL, server);

    VEC_FOR_EACH(&actions.code_actions, struct code_action * action) {
      struct s8 line =
          s8from_fmt("%.*s, (%.*s)", action->title.l, action->title.s,
                     action->kind.l, action->kind.s);
      choice_buffer_add_choice_with_callback(buf, line, action,
                                             code_action_selected);
      s8delete(line);
    }

    VEC_FOR_EACH(&actions.commands, struct lsp_command * command) {
      struct s8 line = s8from_fmt("%.*s", command->title.l, command->title.s);
      choice_buffer_add_choice_with_callback(buf, line, command,
                                             code_action_command_selected);
      s8delete(line);
    }
  }
}

int32_t code_actions_cmd(struct command_ctx ctx, int argc, const char **argv) {
  (void)argc;
  (void)argv;

  struct buffer_view *bv = window_buffer_view(windows_get_active());

  struct lsp_server *server = lsp_server_for_lang_id(bv->buffer->lang.id);
  if (server == NULL) {
    return 0;
  }

  uint64_t id =
      new_pending_request(server, handle_code_actions_response, ctx.buffers);
  struct versioned_text_document_identifier doc =
      versioned_identifier_from_buffer(bv->buffer);
  struct code_action_params params = {
      .text_document.uri = doc.uri,
      .range = region_new(bv->dot, bv->dot),
  };

  VEC_INIT(&params.context.diagnostics, 8);

  struct lsp_buffer_diagnostics *d =
      diagnostics_for_buffer(lsp_server_diagnostics(server), bv->buffer);
  if (d != NULL) {
    VEC_FOR_EACH(&d->diagnostics, struct diagnostic * diag) {
      if (location_is_between(bv->dot, diag->region.begin, diag->region.end)) {
        VEC_PUSH(&params.context.diagnostics, *diag);
      }
    }
  }

  struct s8 json_payload = code_action_params_to_json(&params);
  lsp_send(lsp_backend(server),
           lsp_create_request(id, s8("textDocument/codeAction"), json_payload));

  VEC_DESTROY(&params.context.diagnostics);
  versioned_text_document_identifier_free(&doc);
  s8delete(json_payload);
  return 0;
}

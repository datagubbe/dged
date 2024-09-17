#include "format.h"

#include "completion.h"
#include "dged/buffer.h"
#include "dged/buffer_view.h"
#include "dged/minibuffer.h"
#include "dged/settings.h"
#include "dged/window.h"
#include "main/completion.h"
#include "main/lsp.h"

struct formatted_buffer {
  struct buffer *buffer;
  bool save;
};

static uint32_t get_tab_width(struct buffer *buffer) {
  struct setting *tw = lang_setting(&buffer->lang, "tab-width");
  if (tw == NULL) {
    tw = settings_get("editor.tab-width");
  }

  uint32_t tab_width = 4;
  if (tw != NULL && tw->value.type == Setting_Number) {
    tab_width = tw->value.data.number_value;
  }
  return tab_width;
}

static bool use_tabs(struct buffer *buffer) {
  struct setting *ut = lang_setting(&buffer->lang, "use-tabs");
  if (ut == NULL) {
    ut = settings_get("editor.use-tabs");
  }

  bool use_tabs = false;
  if (ut != NULL && ut->value.type == Setting_Bool) {
    use_tabs = ut->value.data.bool_value;
  }

  return use_tabs;
}

static struct formatting_options options_from_lang(struct buffer *buffer) {
  return (struct formatting_options){
      .tab_size = get_tab_width(buffer),
      .use_spaces = !use_tabs(buffer),
  };
}

void handle_format_response(struct lsp_server *server,
                            struct lsp_response *response, void *userdata) {

  text_edit_vec edits = text_edits_from_json(&response->value.result);
  struct formatted_buffer *buffer = (struct formatted_buffer *)userdata;

  pause_completion();
  if (!VEC_EMPTY(&edits)) {
    apply_edits_buffer(server, buffer->buffer, edits, NULL);

    if (buffer->save) {
      buffer_to_file(buffer->buffer);
    }
  }
  resume_completion();

  text_edits_free(edits);
  free(buffer);
}

static void format_buffer(struct lsp_server *server, struct buffer *buffer,
                          bool save) {
  struct formatted_buffer *b =
      (struct formatted_buffer *)calloc(1, sizeof(struct formatted_buffer));
  b->buffer = buffer;
  b->save = save;

  uint64_t id = new_pending_request(server, handle_format_response, b);
  struct versioned_text_document_identifier doc =
      versioned_identifier_from_buffer(buffer);

  struct document_formatting_params params = {
      .text_document.uri = doc.uri,
      .options = options_from_lang(buffer),
  };

  struct s8 json_payload = document_formatting_params_to_json(&params);
  lsp_send(lsp_backend(server),
           lsp_create_request(id, s8("textDocument/formatting"), json_payload));

  versioned_text_document_identifier_free(&doc);
  s8delete(json_payload);
}

void format_document(struct lsp_server *server, struct buffer *buffer) {
  format_buffer(server, buffer, false);
}

void format_document_save(struct lsp_server *server, struct buffer *buffer) {
  format_buffer(server, buffer, true);
}

void format_region(struct lsp_server *server, struct buffer *buffer,
                   struct region region) {
  struct formatted_buffer *b =
      (struct formatted_buffer *)calloc(1, sizeof(struct formatted_buffer));
  b->buffer = buffer;
  b->save = false;

  uint64_t id = new_pending_request(server, handle_format_response, b);
  struct versioned_text_document_identifier doc =
      versioned_identifier_from_buffer(buffer);

  struct document_range_formatting_params params = {
      .text_document.uri = doc.uri,
      .range = region_to_lsp(buffer, region, server),
      .options = options_from_lang(buffer),
  };

  struct s8 json_payload = document_range_formatting_params_to_json(&params);
  lsp_send(lsp_backend(server),
           lsp_create_request(id, s8("textDocument/formatting"), json_payload));

  versioned_text_document_identifier_free(&doc);
  s8delete(json_payload);
}

int32_t format_cmd(struct command_ctx ctx, int argc, const char **argv) {
  (void)ctx;
  (void)argc;
  (void)argv;

  struct buffer_view *bv = window_buffer_view(windows_get_active());

  struct lsp_server *server = lsp_server_for_lang_id(bv->buffer->lang.id);
  if (server == NULL) {
    return 0;
  }

  struct region reg = region_new(bv->dot, bv->mark);
  if (bv->mark_set && region_has_size(reg)) {
    buffer_view_clear_mark(bv);
    format_region(server, bv->buffer, reg);
  } else {
    format_document(server, bv->buffer);
  }

  return 0;
}

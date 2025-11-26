#include "lsp.h"

#include "dged/binding.h"
#include "dged/buffer.h"
#include "dged/buffer_view.h"
#include "dged/buffers.h"
#include "dged/command.h"
#include "dged/display.h"
#include "dged/hash.h"
#include "dged/hashmap.h"
#include "dged/lang.h"
#include "dged/location.h"
#include "dged/minibuffer.h"
#include "dged/reactor.h"
#include "dged/settings.h"
#include "dged/vec.h"
#include "dged/window.h"

#include "lsp/references.h"
#include "main/bindings.h"
#include "main/completion.h"

#include "lsp/actions.h"
#include "lsp/choice-buffer.h"
#include "lsp/completion.h"
#include "lsp/diagnostics.h"
#include "lsp/format.h"
#include "lsp/goto.h"
#include "lsp/help.h"
#include "lsp/rename.h"

struct lsp_pending_request {
  uint64_t request_id;
  response_handler handler;
  void *userdata;
};

#define MAX_PENDING_REQUESTS 64

struct lsp_server {
  struct lsp *lsp;
  uint32_t restarts;
  struct s8 lang_id;
  struct lsp_pending_request pending_requests[MAX_PENDING_REQUESTS];

  bool initialized;

  enum text_document_sync_kind sync_kind;
  bool send_open_close;
  bool send_save;

  enum position_encoding_kind position_encoding;

  struct lsp_diagnostics *diagnostics;
  struct completion_ctx *completion_ctx;
};

HASHMAP_ENTRY_TYPE(lsp_entry, struct lsp_server);

static struct lsp_data {
  HASHMAP(struct lsp_entry) clients;
  struct keymap keymap;
  struct keymap all_keymap;

  struct reactor *reactor;
  struct buffers *buffers;

  struct buffer *current_diagnostic_buffer;
  uint64_t current_request_id;
} g_lsp_data;

struct lsp *lsp_backend(struct lsp_server *server) { return server->lsp; }

struct lsp_server *lsp_server_for_lang_id(const char *id) {
  HASHMAP_GET(&g_lsp_data.clients, struct lsp_entry, id,
              struct lsp_server * server);
  return server;
}

static uint32_t bytepos_to_column(struct text_chunk *text, uint32_t bytecol) {
  struct utf8_codepoint_iterator iter =
      create_utf8_codepoint_iterator(text->text, text->nbytes, 0);
  uint32_t ncols = 0, col = 0;
  struct codepoint *codepoint = NULL;
  while ((codepoint = utf8_next_codepoint(&iter)) != NULL && col < bytecol) {
    col += codepoint->nbytes;
    ncols += unicode_visual_char_width(codepoint);
  }

  return ncols;
}

static uint32_t codepoint_pos_to_column(struct text_chunk *text,
                                        uint32_t codepoint_col) {
  struct utf8_codepoint_iterator iter =
      create_utf8_codepoint_iterator(text->text, text->nbytes, 0);
  uint32_t ncols = 0, col = 0;
  struct codepoint *codepoint = NULL;
  while ((codepoint = utf8_next_codepoint(&iter)) != NULL &&
         col < codepoint_col) {
    ++col;
    ncols += unicode_visual_char_width(codepoint);
  }

  return ncols;
}

static uint32_t codeunit_pos_to_column(struct text_chunk *text,
                                       uint32_t codeunit_col) {
  struct utf8_codepoint_iterator iter =
      create_utf8_codepoint_iterator(text->text, text->nbytes, 0);
  uint32_t ncols = 0, col = 0;
  struct codepoint *codepoint = NULL;
  while ((codepoint = utf8_next_codepoint(&iter)) != NULL &&
         col < codeunit_col) {
    col += codepoint->codepoint >= 0x010000 ? 2 : 1;
    ncols += unicode_visual_char_width(codepoint);
  }

  return ncols;
}

struct region lsp_range_to_coordinates(struct lsp_server *server,
                                       struct buffer *buffer,
                                       struct region range) {

  uint32_t (*col_converter)(struct text_chunk *, uint32_t) =
      codeunit_pos_to_column;

  switch (server->position_encoding) {
  case PositionEncoding_Utf8:
    col_converter = bytepos_to_column;
    break;

  case PositionEncoding_Utf16:
    col_converter = codeunit_pos_to_column;
    break;

  case PositionEncoding_Utf32:
    col_converter = codepoint_pos_to_column;
    break;
  }

  struct region reg = range;
  struct text_chunk beg_line = buffer_line(buffer, range.begin.line);
  reg.begin.col = col_converter(&beg_line, range.begin.col);

  struct text_chunk end_line = beg_line;
  if (range.begin.line != range.end.line) {
    end_line = buffer_line(buffer, range.end.line);
  }
  reg.end.col = col_converter(&end_line, range.end.col);

  if (beg_line.allocated) {
    free(beg_line.text);
  }

  if (range.begin.line != range.end.line && end_line.allocated) {
    free(end_line.text);
  }

  return reg;
}

uint64_t new_pending_request(struct lsp_server *server,
                             response_handler handler, void *userdata) {
  ++g_lsp_data.current_request_id;
  for (int i = 0; i < MAX_PENDING_REQUESTS; ++i) {
    if (server->pending_requests[i].request_id == (uint64_t)-1) {
      server->pending_requests[i].request_id = g_lsp_data.current_request_id;
      server->pending_requests[i].handler = handler;
      server->pending_requests[i].userdata = userdata;
      return g_lsp_data.current_request_id;
    }
  }

  return g_lsp_data.current_request_id;
}

static bool
request_response_received(struct lsp_server *server, uint64_t id,
                          struct lsp_pending_request **pending_request) {
  for (int i = 0; i < MAX_PENDING_REQUESTS; ++i) {
    if (server->pending_requests[i].request_id == id) {
      server->pending_requests[i].request_id = (uint64_t)-1;
      *pending_request = &server->pending_requests[i];
      return true;
    }
  }

  return false;
}

static struct region edit_location_to_lsp(struct buffer *buffer,
                                          struct edit_location edit,
                                          struct lsp_server *server) {

  struct region res = edit.coordinates;
  if (server->position_encoding == PositionEncoding_Utf8) {
    /* In this case, the buffer hook has already
     * done the job for us. */
    res.begin.col = edit.bytes.begin.col;
    res.end.col = edit.bytes.end.col;
    return res;
  }

  return region_to_lsp(buffer, res, server);
}

static void buffer_reloaded(struct buffer *buffer, void *userdata) {
  struct lsp_server *server = (struct lsp_server *)userdata;

  struct text_chunk new_text = buffer_text(buffer);

  struct text_document_content_change_event evt = {
      .full_document = true,
      .text = s8new((const char *)new_text.text, new_text.nbytes),
      .range = region_new(
          (struct location){.col = 0, .line = 0},
          (struct location){.col = 0, .line = buffer_num_lines(buffer)}),
  };

  struct versioned_text_document_identifier text_document =
      versioned_identifier_from_buffer(buffer);
  struct did_change_text_document_params params = {
      .text_document = text_document,
      .content_changes = &evt,
      .ncontent_changes = 1,
  };

  struct s8 json_payload = did_change_text_document_params_to_json(&params);

  lsp_send(server->lsp,
           lsp_create_notification(s8("textDocument/didChange"), json_payload));

  versioned_text_document_identifier_free(&text_document);
  s8delete(json_payload);
  s8delete(evt.text);

  if (new_text.allocated) {
    free(new_text.text);
  }
}

static void buffer_updated(struct buffer *buffer, void *userdata) {
  struct lsp_server *server = (struct lsp_server *)userdata;

  struct lsp_buffer_diagnostics *diagnostics =
      diagnostics_for_buffer(server->diagnostics, buffer);
  if (diagnostics == NULL) {
    return;
  }

  buffer_clear_text_property_layer(buffer, diagnostics->layer);

  VEC_FOR_EACH(&diagnostics->diagnostics, struct diagnostic * diag) {
    struct text_property prop;
    prop.type = TextProperty_Colors;
    uint32_t color = diag_severity_color(diag->severity);
    prop.data.colors = (struct text_property_colors){
        .set_bg = true,
        .set_fg = true,
        .fg = Color_BrightWhite,
        .bg = color,
        .underline = true,
    };

    struct region reg = region_new(
        diag->region.begin, buffer_previous_char(buffer, diag->region.end));

    buffer_add_text_property_to_layer(buffer, reg.begin, reg.end, prop,
                                      diagnostics->layer);

    if (window_buffer(windows_get_active()) == buffer) {
      struct buffer_view *bv = window_buffer_view(windows_get_active());

      if (region_is_inside(diag->region, bv->dot)) {
        size_t len = 0;
        for (size_t i = 0; i < diag->message.l && diag->message.s[i] != '\n';
             ++i) {
          ++len;
        }
        minibuffer_display_timeout(1, "%s: %.*s",
                                   diag_severity_to_str(diag->severity), len,
                                   diag->message.s);
      }
    }
  }
}

static void buffer_pre_save(struct buffer *buffer, void *userdata) {
  (void)buffer;
  (void)userdata;
}

static void format_on_save(struct buffer *buffer, struct lsp_server *server) {
  struct setting *glob_fmt_on_save = settings_get("editor.format-on-save");
  struct setting *fmt_on_save =
      lang_setting(&buffer->lang, "language-server.format-on-save");

  if ((glob_fmt_on_save != NULL && glob_fmt_on_save->value.data.bool_value) ||
      (glob_fmt_on_save == NULL && fmt_on_save != NULL &&
       fmt_on_save->value.data.bool_value)) {
    format_document_save(server, buffer);
  }
}

static void buffer_post_save(struct buffer *buffer, void *userdata) {
  struct lsp_server *server = (struct lsp_server *)userdata;
  if (server->send_save) {
    struct versioned_text_document_identifier text_document =
        versioned_identifier_from_buffer(buffer);

    struct did_save_text_document_params params = {
        .text_document =
            (struct text_document_identifier){
                .uri = text_document.uri,
            },
    };

    struct s8 json_payload = did_save_text_document_params_to_json(&params);

    lsp_send(server->lsp,
             lsp_create_notification(s8("textDocument/didSave"), json_payload));

    versioned_text_document_identifier_free(&text_document);
    s8delete(json_payload);
  }

  format_on_save(buffer, server);
}

static uint32_t count_codepoints(struct text_chunk *chunk,
                                 uint32_t target_col) {
  struct utf8_codepoint_iterator iter =
      create_utf8_codepoint_iterator(chunk->text, chunk->nbytes, 0);
  uint32_t ncodepoints = 0, col = 0;
  struct codepoint *codepoint = NULL;
  while ((codepoint = utf8_next_codepoint(&iter)) != NULL && col < target_col) {
    col += unicode_visual_char_width(codepoint);
    ++ncodepoints;
  }

  return ncodepoints;
}

static uint32_t count_codeunits(struct text_chunk *chunk, uint32_t target_col) {
  struct utf8_codepoint_iterator iter =
      create_utf8_codepoint_iterator(chunk->text, chunk->nbytes, 0);
  uint32_t ncodeunits = 0, col = 0;
  struct codepoint *codepoint = NULL;
  while ((codepoint = utf8_next_codepoint(&iter)) != NULL && col < target_col) {
    col += unicode_visual_char_width(codepoint);
    ncodeunits += codepoint->codepoint >= 0x010000 ? 2 : 1;
  }

  return ncodeunits;
}

static uint32_t count_bytes(struct text_chunk *chunk, uint32_t target_col) {
  struct utf8_codepoint_iterator iter =
      create_utf8_codepoint_iterator(chunk->text, chunk->nbytes, 0);
  uint32_t nbytes = 0, col = 0;
  struct codepoint *codepoint = NULL;
  while ((codepoint = utf8_next_codepoint(&iter)) != NULL && col < target_col) {
    col += unicode_visual_char_width(codepoint);
    nbytes += codepoint->nbytes;
  }

  return nbytes;
}

struct region region_to_lsp(struct buffer *buffer, struct region region,
                            struct lsp_server *server) {
  struct region res = region;

  uint32_t (*col_counter)(struct text_chunk *, uint32_t) = count_codeunits;

  switch (server->position_encoding) {
  case PositionEncoding_Utf8:
    col_counter = count_bytes;
    return res;

  case PositionEncoding_Utf16:
    col_counter = count_codeunits;
    break;

  case PositionEncoding_Utf32:
    col_counter = count_codepoints;
    break;
  }

  struct text_chunk beg_line = buffer_line(buffer, region.begin.line);
  res.begin.col = col_counter(&beg_line, region.begin.col);

  struct text_chunk end_line = beg_line;
  if (region.begin.line != region.end.line) {
    end_line = buffer_line(buffer, region.end.line);
  }

  res.end.col = col_counter(&end_line, region.end.col);

  if (beg_line.allocated) {
    free(beg_line.text);
  }

  if (end_line.allocated && region.begin.line != region.end.line) {
    free(end_line.text);
  }

  return res;
}

static void buffer_text_changed(struct buffer *buffer,
                                struct edit_location range, bool delete,
                                void *userdata) {
  struct lsp_server *server = (struct lsp_server *)userdata;

  struct text_chunk new_text = {0};
  switch (server->sync_kind) {
  case TextDocumentSync_None:
    return;

  case TextDocumentSync_Full:
    new_text = buffer_text(buffer);
    break;

  case TextDocumentSync_Incremental:
    if (!delete) {
      new_text = buffer_region(buffer, range.coordinates);
    }
    break;
  }

  struct region reg = edit_location_to_lsp(buffer, range, server);
  reg = delete ? reg : region_new(reg.begin, reg.begin);
  struct text_document_content_change_event evt = {
      .full_document = server->sync_kind == TextDocumentSync_Full,
      .text = s8new((const char *)new_text.text, new_text.nbytes),
      .range = reg,
  };

  struct versioned_text_document_identifier text_document =
      versioned_identifier_from_buffer(buffer);
  struct did_change_text_document_params params = {
      .text_document = text_document,
      .content_changes = &evt,
      .ncontent_changes = 1,
  };

  struct s8 json_payload = did_change_text_document_params_to_json(&params);

  lsp_send(server->lsp,
           lsp_create_notification(s8("textDocument/didChange"), json_payload));

  versioned_text_document_identifier_free(&text_document);
  s8delete(json_payload);
  s8delete(evt.text);

  if (new_text.allocated) {
    free(new_text.text);
  }
}

static void buffer_text_inserted(struct buffer *buffer,
                                 struct edit_location inserted,
                                 void *userdata) {
  buffer_text_changed(buffer, inserted, false, userdata);
}

static void buffer_text_deleted(struct buffer *buffer,
                                struct edit_location deleted, void *userdata) {
  buffer_text_changed(buffer, deleted, true, userdata);
}

static void send_did_open(struct lsp_server *server, struct buffer *buffer) {
  if (!server->send_open_close) {
    return;
  }

  struct text_document_item doc = text_document_item_from_buffer(buffer);
  struct did_open_text_document_params params = {
      .text_document = doc,
  };

  struct s8 json_payload = did_open_text_document_params_to_json(&params);
  lsp_send(server->lsp,
           lsp_create_notification(s8("textDocument/didOpen"), json_payload));

  text_document_item_free(&doc);
  s8delete(json_payload);
}

static void setup_completion(struct lsp_server *server, struct buffer *buffer) {
  if (server->completion_ctx != NULL) {
    enable_completion_for_buffer(server->completion_ctx, buffer);
  }
}

static void lsp_buffer_initialized(struct lsp_server *server,
                                   struct buffer *buffer) {
  if (s8eq(server->lang_id, s8(buffer->lang.id))) {
    /* Needs to be a pre-delete hook since we need
     * access to the deleted content to derive the
     * correct UTF-8/16/32 position.
     */
    buffer_add_pre_delete_hook(buffer, buffer_text_deleted, server);
    buffer_add_insert_hook(buffer, buffer_text_inserted, server);
    buffer_add_update_hook(buffer, buffer_updated, server);
    buffer_add_pre_save_hook(buffer, buffer_pre_save, server);
    buffer_add_post_save_hook(buffer, buffer_post_save, server);
    buffer_add_reload_hook(buffer, buffer_reloaded, server);

    send_did_open(server, buffer);
    setup_completion(server, buffer);
  }
}

static void apply_initialized(struct buffer *buffer, void *userdata) {
  struct lsp_server *server = (struct lsp_server *)userdata;
  lsp_buffer_initialized(server, buffer);
}

static void handle_initialize(struct lsp_server *server,
                              struct lsp_response *response, void *userdata) {
  (void)userdata;

  struct initialize_result res =
      initialize_result_from_json(&response->value.result);
  message("lsp server initialized: %.*s (%.*s)", res.server_info.name.l,
          res.server_info.name.s, res.server_info.version.l,
          res.server_info.version.s);

  lsp_send(server->lsp, lsp_create_notification(s8("initialized"), s8("")));

  struct text_document_sync *tsync = &res.capabilities.text_document_sync;
  server->sync_kind = tsync->kind;
  server->send_open_close = tsync->open_close;
  server->send_save = tsync->save;
  server->position_encoding = res.capabilities.position_encoding;

  if (res.capabilities.supports_completion) {
    struct completion_options *comp_opts = &res.capabilities.completion_options;
    server->completion_ctx = create_completion_ctx(
        server, (triggerchar_vec *)&comp_opts->trigger_characters);
  }

  initialize_result_free(&res);
  buffers_for_each(g_lsp_data.buffers, apply_initialized, server);

  server->initialized = true;
}

static void init_lsp_client(struct lsp_server *server) {
  if (lsp_restart_server(server->lsp) < 0) {
    minibuffer_echo("failed to start language server %s process.",
                    lsp_server_name(server->lsp));
    return;
  }

  // send some init info
  struct initialize_params params = {
      .process_id = lsp_server_pid(server->lsp),
      .client_info =
          {
              .name = s8("dged"),
              .version = s8("dev"),
          },
      .client_capabilities = {},
      .workspace_folders = NULL,
      .nworkspace_folders = 0,
  };

  uint64_t id = new_pending_request(server, handle_initialize, NULL);
  struct s8 json_payload = initialize_params_to_json(&params);
  lsp_send(server->lsp, lsp_create_request(id, s8("initialize"), json_payload));

  s8delete(json_payload);
}

static void create_lsp_client(struct buffer *buffer, void *userdata) {
  (void)userdata;
  const char *id = buffer->lang.id;
  HASHMAP_GET(&g_lsp_data.clients, struct lsp_entry, id,
              struct lsp_server * server);
  if (server == NULL) {
    // we need to start a new server
    struct setting *s = lang_setting(&buffer->lang, "language-server.command");
    if (!s) {
      if (!lang_is_fundamental(&buffer->lang)) {
        message("No language server set for %s. Set with "
                "`languages.%s.language-server`.",
                buffer->lang.id, buffer->lang.id);
      }
      return;
    }

    char *const command[] = {s->value.data.string_value, NULL};

    char bufname[1024] = {0};
    snprintf(bufname, 1024, "*%s-lsp-stderr*", command[0]);
    struct buffer *stderr_buf = buffers_find(g_lsp_data.buffers, bufname);
    if (stderr_buf == NULL) {
      struct buffer buf = buffer_create(bufname);
      buf.lazy_row_add = false;
      stderr_buf = buffers_add(g_lsp_data.buffers, buf);
      buffer_set_readonly(stderr_buf, true);
    }

    struct lsp *new_lsp =
        lsp_create(command, g_lsp_data.reactor, stderr_buf, command[0]);

    if (new_lsp == NULL) {
      minibuffer_echo("failed to create language server %s", command[0]);
      buffers_remove(g_lsp_data.buffers, bufname);
      return;
    }

    HASHMAP_APPEND(&g_lsp_data.clients, struct lsp_entry, id,
                   struct lsp_entry * new);
    new->value = (struct lsp_server){
        .lsp = new_lsp,
        .lang_id = s8new(id, strlen(id)),
        .restarts = 0,
    };
    for (int i = 0; i < 16; ++i) {
      new->value.pending_requests[i].request_id = (uint64_t)-1;
    }

    new->value.diagnostics = diagnostics_create();

    // support for this is determined later
    new->value.completion_ctx = NULL;

    init_lsp_client(&new->value);
    server = &new->value;
  }

  /* An lsp for the language for this buffer is already started.
   * if server is not initialized, it will get picked
   * up anyway when handling the initialize response. */
  if (server->initialized) {
    lsp_buffer_initialized(server, buffer);
  }
}

static void set_default_lsp(const char *lang_id, const char *command) {
  struct language l = lang_from_id(lang_id);
  if (!lang_is_fundamental(&l)) {
    lang_setting_set_default(
        &l, "language-server.command",
        (struct setting_value){.type = Setting_String,
                               .data.string_value = (char *)command});
    lang_setting_set_default(
        &l, "language-server.format-on-save",
        (struct setting_value){.type = Setting_Bool, .data.bool_value = false});

    lang_destroy(&l);
  }
}

static struct s8 lsp_modeline(struct buffer_view *view, void *userdata) {
  (void)userdata;
  struct lsp_server *server = lsp_server_for_lang_id(view->buffer->lang.id);
  if (server == NULL) {
    return s8("");
  }

  return s8from_fmt(
      "lsp: %s:%d", lsp_server_name(server->lsp),
      lsp_server_running(server->lsp) ? lsp_server_pid(server->lsp) : 0);
}

static uint32_t lsp_keymap_hook(struct buffer *buffer, struct keymap *keymaps[],
                                uint32_t max_nkeymaps, void *userdata) {
  (void)userdata;

  if (max_nkeymaps < 2) {
    return 0;
  }

  uint32_t nadded = 1;
  keymaps[0] = &g_lsp_data.all_keymap;

  if (lsp_server_for_lang_id(buffer->lang.id) != NULL) {
    keymaps[1] = &g_lsp_data.keymap;
    nadded = 2;
  }

  return nadded;
}

static int32_t lsp_restart_cmd(struct command_ctx ctx, int argc,
                               const char **argv) {
  (void)ctx;
  (void)argc;
  (void)argv;
  struct buffer *b = window_buffer(windows_get_active());

  struct lsp_server *server = lsp_server_for_buffer(b);
  if (server == NULL) {
    return 0;
  }

  lsp_restart_server(server->lsp);
  return 0;
}

void lang_servers_init(struct reactor *reactor, struct buffers *buffers,
                       struct commands *commands) {
  HASHMAP_INIT(&g_lsp_data.clients, 32, hash_name);

  set_default_lsp("c", "clangd");
  set_default_lsp("cxx", "clangd");
  set_default_lsp("rs", "rust-analyzer");
  set_default_lsp("python", "pylsp");

  g_lsp_data.current_request_id = 0;
  g_lsp_data.reactor = reactor;
  g_lsp_data.buffers = buffers;
  buffers_add_add_hook(buffers, create_lsp_client, NULL);

  struct command lsp_commands[] = {
      {.name = "lsp-goto-definition", .fn = lsp_goto_def_cmd},
      {.name = "lsp-goto-declaration", .fn = lsp_goto_decl_cmd},
      {.name = "lsp-goto-implementation", .fn = lsp_goto_impl_cmd},
      {.name = "lsp-goto", .fn = lsp_goto_cmd},
      {.name = "lsp-goto-previous", .fn = lsp_goto_previous_cmd},
      {.name = "lsp-references", .fn = lsp_references_cmd},
      {.name = "lsp-restart", .fn = lsp_restart_cmd},
      {.name = "lsp-diagnostics", .fn = diagnostics_cmd},
      {.name = "lsp-next-diagnostic", .fn = next_diagnostic_cmd},
      {.name = "lsp-prev-diagnostic", .fn = prev_diagnostic_cmd},
      {.name = "lsp-code-actions", .fn = code_actions_cmd},
      {.name = "lsp-format", .fn = format_cmd},
      {.name = "lsp-rename", .fn = lsp_rename_cmd},
      {.name = "lsp-help", .fn = lsp_help_cmd},
  };

  register_commands(commands, lsp_commands,
                    sizeof(lsp_commands) / sizeof(lsp_commands[0]));

  struct binding lsp_binds[] = {
      BINDING(Meta, '.', "lsp-goto-definition"),
      BINDING(Meta, '/', "lsp-goto"),
      BINDING(Meta, '[', "lsp-prev-diagnostic"),
      BINDING(Meta, ']', "lsp-next-diagnostic"),
      BINDING(Meta, 'a', "lsp-code-actions"),
      BINDING(Meta, '=', "lsp-format"),
      BINDING(Meta, 'r', "lsp-rename"),
      BINDING(Meta, 'h', "lsp-help"),
  };

  struct binding global_binds[] = {
      BINDING(Meta, ',', "lsp-goto-previous"),
  };

  g_lsp_data.keymap = keymap_create("lsp", 32);
  keymap_bind_keys(&g_lsp_data.keymap, lsp_binds,
                   sizeof(lsp_binds) / sizeof(lsp_binds[0]));
  g_lsp_data.all_keymap = keymap_create("lsp-global", 32);
  keymap_bind_keys(&g_lsp_data.all_keymap, global_binds,
                   sizeof(global_binds) / sizeof(global_binds[0]));
  buffer_add_keymaps_hook(lsp_keymap_hook, NULL);

  buffer_view_add_modeline_hook(lsp_modeline, NULL);

  init_goto(32, buffers);
}

void apply_edits_buffer(struct lsp_server *server, struct buffer *buffer,
                        text_edit_vec edits, struct location *point) {
  VEC_FOR_EACH_REVERSE(&edits, struct text_edit * edit) {
    struct region reg = lsp_range_to_coordinates(server, buffer, edit->range);
    struct location at = reg.end;
    if (region_has_size(reg)) {
      if (point != NULL) {

        if (reg.end.line == point->line) {
          point->col -= reg.end.col > point->col ? 0 : point->col - reg.end.col;
        }

        uint64_t lines_deleted = reg.end.line - reg.begin.line;
        if (lines_deleted > 0 && reg.end.line <= point->line) {
          point->line -= lines_deleted;
        }
      }
      at = buffer_delete(buffer, reg);
    }

    struct location after =
        buffer_add(buffer, at, edit->new_text.s, edit->new_text.l);
    if (point != NULL) {
      if (after.line == point->line) {
        point->col += after.col;
      }

      uint64_t lines_added = after.line - at.line;
      if (lines_added > 0 && after.line <= point->line) {
        point->line += lines_added;
      }
    }
  }
}

bool apply_edits(struct lsp_server *server,
                 const struct workspace_edit *ws_edit) {
  pause_completion();

  VEC_FOR_EACH(&ws_edit->changes, struct text_edit_pair * pair) {
    if (VEC_EMPTY(&pair->edits)) {
      continue;
    }

    const char *p = s8tocstr(pair->uri);
    struct buffer *b = buffers_find_by_filename(g_lsp_data.buffers, &p[7]);

    if (b == NULL) {
      struct buffer new_buf = buffer_from_file(&p[7]);
      b = buffers_add(g_lsp_data.buffers, new_buf);
    }

    free((void *)p);
    buffer_push_undo_boundary(b);
    apply_edits_buffer(server, b, pair->edits, NULL);
    buffer_push_undo_boundary(b);
  }

  VEC_FOR_EACH(&ws_edit->document_changes, struct text_document_edit * edit) {
    if (VEC_EMPTY(&edit->edits)) {
      continue;
    }

    const char *p = s8tocstr(edit->text_document.uri);
    struct buffer *b = buffers_find_by_filename(g_lsp_data.buffers, &p[7]);

    if (b == NULL) {
      struct buffer new_buf = buffer_from_file(&p[7]);
      b = buffers_add(g_lsp_data.buffers, new_buf);
    }

    free((void *)p);
    buffer_push_undo_boundary(b);
    apply_edits_buffer(server, b, edit->edits, NULL);
    buffer_push_undo_boundary(b);
  }

  resume_completion();
  return true;
}

static void handle_request(struct lsp_server *server,
                           struct lsp_request request) {

  struct s8 method = unescape_json_string(request.method);
  if (s8eq(method, s8("workspace/applyEdit"))) {
    struct workspace_edit ws_edit = workspace_edit_from_json(&request.params);
    apply_edits(server, &ws_edit);
    workspace_edit_free(&ws_edit);
  } else {
    message("unhandled lsp request (%s): id %d: %.*s",
            lsp_server_name(server->lsp), request.id, request.method.l,
            request.method.s);
  }

  s8delete(method);
}

static void handle_response(struct lsp_server *server,
                            struct lsp_response response) {
  if (response.ok) {
    struct lsp_pending_request *pending = NULL;
    if (!request_response_received(server, response.id, &pending)) {
      message("received response for id %d, server %s, which has no handler "
              "registered",
              response.id, lsp_server_name(server->lsp));
      return;
    }

    if (pending->handler != NULL) {
      pending->handler(server, &response, pending->userdata);
    }
  } else {
    struct s8 errmsg = response.value.error.message;
    minibuffer_echo("lsp error (%s), id %d: %.*s", lsp_server_name(server->lsp),
                    response.id, errmsg.l, errmsg.s);
  }
}

static void handle_notification(struct lsp_server *server,
                                struct lsp_notification notification) {
  struct s8 method = unescape_json_string(notification.method);
  if (s8eq(method, s8("textDocument/publishDiagnostics"))) {
    handle_publish_diagnostics(server, g_lsp_data.buffers, &notification);
  }

  s8delete(method);
}

#define MAX_RESTARTS 10

static void restart_if_needed(struct lsp_server *server) {
  // if we successfully initialized the server, we can be sure
  // it is up and running
  if (lsp_server_running(server->lsp) && server->initialized) {
    server->restarts = 0;
    return;
  }

  if (!lsp_server_running(server->lsp)) {
    if (server->restarts < MAX_RESTARTS) {
      message("restarting \"%s\" (%d/%d)...", lsp_server_name(server->lsp),
              server->restarts + 1, MAX_RESTARTS);
      init_lsp_client(server);
      ++server->restarts;

      if (server->restarts == MAX_RESTARTS) {
        minibuffer_echo("lsp \"%s\" has crashed %d times, giving up...",
                        lsp_server_name(server->lsp), MAX_RESTARTS);
      }
    } else {
      // server is crashed and can only be restarted manually now
      lsp_stop_server(server->lsp);
    }
  }
}

void lang_servers_update(void) {

  HASHMAP_FOR_EACH(&g_lsp_data.clients, struct lsp_entry * e) {
    restart_if_needed(&e->value);

    struct lsp_message msgs[128];
    uint32_t msgs_received = lsp_update(e->value.lsp, msgs, 128);

    if (msgs_received == 0 || msgs_received == (uint32_t)-1) {
      continue;
    }

    char bufname[1024] = {0};
    snprintf(bufname, 1024, "*%s-lsp-messages*", lsp_server_name(e->value.lsp));
    struct buffer *output_buf = buffers_find(g_lsp_data.buffers, bufname);
    if (output_buf == NULL) {
      struct buffer buf = buffer_create(bufname);
      buf.lazy_row_add = false;
      output_buf = buffers_add(g_lsp_data.buffers, buf);
    }

    buffer_set_readonly(output_buf, false);
    for (uint32_t mi = 0; mi < msgs_received; ++mi) {
      struct lsp_message *msg = &msgs[mi];
      buffer_add(output_buf, buffer_end(output_buf), msg->payload.s,
                 msg->payload.l);
      buffer_add(output_buf, buffer_end(output_buf), (uint8_t *)"\n", 1);

      switch (msg->type) {
      case Lsp_Response:
        handle_response(&e->value, msg->message.response);
        break;

      case Lsp_Request:
        handle_request(&e->value, msg->message.request);
        break;

      case Lsp_Notification:
        handle_notification(&e->value, msg->message.notification);
        break;
      }

      lsp_message_destroy(msg);
    }

    buffer_set_readonly(output_buf, true);
  }
}

static void lang_server_teardown(struct lsp_server *server) {
  destroy_goto();
  lsp_stop_server(server->lsp);
  lsp_destroy(server->lsp);
  s8delete(server->lang_id);
}

void lang_servers_teardown(void) {
  HASHMAP_FOR_EACH(&g_lsp_data.clients, struct lsp_entry * e) {
    diagnostics_destroy(e->value.diagnostics);

    if (e->value.completion_ctx != NULL) {
      destroy_completion_ctx(e->value.completion_ctx);
    }

    lang_server_teardown(&e->value);
  }

  keymap_destroy(&g_lsp_data.keymap);
  keymap_destroy(&g_lsp_data.all_keymap);
  HASHMAP_DESTROY(&g_lsp_data.clients);
}

struct lsp_server *lsp_server_for_buffer(struct buffer *buffer) {
  return lsp_server_for_lang_id(buffer->lang.id);
}

struct lsp_diagnostics *lsp_server_diagnostics(struct lsp_server *server) {
  return server->diagnostics;
}

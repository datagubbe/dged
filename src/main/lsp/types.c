#include "types.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "dged/buffer.h"
#include "dged/display.h"
#include "dged/path.h"
#include "dged/s8.h"

struct s8 initialize_params_to_json(struct initialize_params *params) {
  char *cwd = getcwd(NULL, 0);
  const char *fmt =
      "{ \"processId\": %d, \"clientInfo\": { \"name\": "
      "\"%.*s\", \"version\": \"%.*s\" },"
      "\"capabilities\": { \"textDocument\": { "
      "\"publishDiagnostics\": { },"
      "\"hover\": { \"dynamicRegistration\": false, \"contentFormat\": [ "
      "\"plaintext\", \"markdown\" ] },"
      "\"signatureHelp\" : { \"dynamicRegistration\": false, "
      "\"signatureInformation\": {"
      " \"documentationFormat\": [ \"plaintext\", \"markdown\" ], "
      "\"activeParameterSupport\": true } },"
      "\"codeAction\": { \"codeActionLiteralSupport\": { \"codeActionKind\":"
      "{ \"valueSet\": [ \"quickfix\", \"refactor\", \"source\", "
      "\"refactor.extract\", "
      "\"refactor.inline\", \"refactor.rewrite\", \"source.organizeImports\" ] "
      "} } } },"
      "\"general\": { \"positionEncodings\": [ \"utf-8\", "
      "\"utf-32\", \"utf-16\" ] },"
      "\"offsetEncoding\": [ \"utf-8\", \"utf-32\" ,\"utf-16\" ]"
      "},"
      "\"workspaceFolders\": [ { \"uri\": \"file://%s\", "
      "\"name\": \"cwd\" } ] }";

  struct s8 s =
      s8from_fmt(fmt, params->process_id, params->client_info.name.l,
                 params->client_info.name.s, params->client_info.version.l,
                 params->client_info.version.s, cwd);

  free(cwd);
  return s;
}

static enum position_encoding_kind
position_encoding_from_str(struct s8 encoding_kind) {
  if (s8eq(encoding_kind, s8("utf-8"))) {
    return PositionEncoding_Utf8;
  } else if (s8eq(encoding_kind, s8("utf-32"))) {
    return PositionEncoding_Utf32;
  }

  return PositionEncoding_Utf16;
}

struct s8 position_encoding_kind_str(enum position_encoding_kind kind) {
  switch (kind) {
  case PositionEncoding_Utf8:
    return s8("utf-8");

  case PositionEncoding_Utf32:
    return s8("utf-32");

  default:
    break;
  }

  return s8("utf-16");
}

static struct server_capabilities
parse_capabilities(struct json_object *root, struct json_value *capabilities) {
  struct server_capabilities caps = {
      .text_document_sync.kind = TextDocumentSync_Full,
      .text_document_sync.open_close = false,
      .text_document_sync.save = false,
      .position_encoding = PositionEncoding_Utf16,
  };

  // clang has this legacy attribute for positionEncoding
  // use with a lower prio than positionEncoding in capabilities
  struct json_value *offset_encoding = json_get(root, s8("offsetEncoding"));
  if (offset_encoding != NULL && offset_encoding->type == Json_String) {
    caps.position_encoding =
        position_encoding_from_str(offset_encoding->value.string);
  }

  if (capabilities == NULL || capabilities->type != Json_Object) {
    return caps;
  }

  struct json_object *obj = capabilities->value.object;
  // text document sync caps
  struct json_value *text_doc_sync = json_get(obj, s8("textDocumentSync"));
  if (text_doc_sync != NULL) {
    if (text_doc_sync->type == Json_Number) {
      caps.text_document_sync.kind =
          (enum text_document_sync_kind)text_doc_sync->value.number;
    } else {
      struct json_object *tsync = text_doc_sync->value.object;
      caps.text_document_sync.kind =
          (enum text_document_sync_kind)json_get(tsync, s8("change"))
              ->value.number;

      struct json_value *open_close = json_get(tsync, s8("openClose"));
      caps.text_document_sync.open_close =
          open_close != NULL ? open_close->value.boolean : false;

      struct json_value *save = json_get(tsync, s8("save"));
      caps.text_document_sync.save =
          save != NULL ? open_close->value.boolean : false;
    }
  }

  // position encoding
  struct json_value *pos_enc = json_get(obj, s8("positionEncoding"));
  if (pos_enc != NULL && pos_enc->type == Json_String) {
    caps.position_encoding = position_encoding_from_str(pos_enc->value.string);
  }

  struct json_value *completion_opts = json_get(obj, s8("completionProvider"));
  caps.supports_completion = false;
  if (completion_opts != NULL && completion_opts->type == Json_Object) {
    caps.supports_completion = true;

    // trigger chars
    struct json_value *trigger_chars =
        json_get(completion_opts->value.object, s8("triggerCharacters"));
    if (trigger_chars != NULL && trigger_chars->type == Json_Array) {
      uint64_t arrlen = json_array_len(trigger_chars->value.array);
      VEC_INIT(&caps.completion_options.trigger_characters, arrlen);
      for (uint32_t i = 0; i < arrlen; ++i) {
        struct json_value *val = json_array_get(trigger_chars->value.array, i);
        VEC_PUSH(&caps.completion_options.trigger_characters,
                 s8dup(val->value.string));
      }
    }

    // all commit characters
    struct json_value *commit_chars =
        json_get(completion_opts->value.object, s8("allCommitCharacters"));
    if (commit_chars != NULL && commit_chars->type == Json_Array) {
      uint64_t arrlen = json_array_len(commit_chars->value.array);
      VEC_INIT(&caps.completion_options.all_commit_characters, arrlen);
      for (uint32_t i = 0; i < arrlen; ++i) {
        struct json_value *val = json_array_get(commit_chars->value.array, i);
        VEC_PUSH(&caps.completion_options.all_commit_characters,
                 s8dup(val->value.string));
      }
    }

    // resolve provider
    struct json_value *resolve_provider =
        json_get(completion_opts->value.object, s8("resolveProvider"));
    if (resolve_provider != NULL && resolve_provider->type == Json_Bool) {
      caps.completion_options.resolve_provider =
          resolve_provider->value.boolean;
    }
  }

  return caps;
}

struct initialize_result initialize_result_from_json(struct json_value *json) {
  struct json_object *obj = json->value.object;
  struct json_object *server_info =
      json_get(obj, s8("serverInfo"))->value.object;
  return (struct initialize_result){
      .capabilities =
          parse_capabilities(obj, json_get(obj, s8("capabilities"))),
      .server_info.name =
          s8dup(json_get(server_info, s8("name"))->value.string),
      .server_info.version =
          s8dup(json_get(server_info, s8("version"))->value.string),
  };
}

void initialize_result_free(struct initialize_result *res) {
  s8delete(res->server_info.name);
  s8delete(res->server_info.version);
  if (res->capabilities.supports_completion) {
    VEC_FOR_EACH(&res->capabilities.completion_options.trigger_characters,
                 struct s8 * s) {
      s8delete(*s);
    }

    VEC_DESTROY(&res->capabilities.completion_options.trigger_characters);

    VEC_FOR_EACH(&res->capabilities.completion_options.all_commit_characters,
                 struct s8 * s) {
      s8delete(*s);
    }

    VEC_DESTROY(&res->capabilities.completion_options.all_commit_characters);
  }
}

static struct s8 uri_from_buffer(struct buffer *buffer) {
  if (buffer->filename != NULL) {
    char *abspath = to_abspath(buffer->filename);
    struct s8 ret = s8from_fmt("file://%s", abspath);
    free(abspath);
    return ret;
  }

  return s8from_fmt("file://invalid-file");
}

struct text_document_item
text_document_item_from_buffer(struct buffer *buffer) {
  struct text_chunk buffer_text =
      buffer_region(buffer, region_new((struct location){.line = 0, .col = 0},
                                       buffer_end(buffer)));
  struct text_document_item item = {
      .uri = uri_from_buffer(buffer),
      .language_id = s8new(buffer->lang.id, strlen(buffer->lang.id)),
      .version = buffer->version,
      .text =
          (struct s8){
              .s = buffer_text.text,
              .l = buffer_text.nbytes,
          },
  };

  return item;
}

void text_document_item_free(struct text_document_item *item) {
  s8delete(item->uri);
  s8delete(item->language_id);
  s8delete(item->text);
}

struct versioned_text_document_identifier
versioned_identifier_from_buffer(struct buffer *buffer) {
  struct versioned_text_document_identifier identifier = {
      .uri = uri_from_buffer(buffer),
      .version = buffer->version,
  };

  return identifier;
}

void versioned_text_document_identifier_free(
    struct versioned_text_document_identifier *identifier) {
  s8delete(identifier->uri);
}

struct s8 did_change_text_document_params_to_json(
    struct did_change_text_document_params *params) {
  size_t event_buf_size = 0;
  for (size_t i = 0; i < params->ncontent_changes; ++i) {
    struct text_document_content_change_event *ev = &params->content_changes[i];
    struct s8 escaped = escape_json_string(ev->text);
    if (!ev->full_document) {
      const char *item_fmt =
          "{ \"range\": { \"start\": { \"line\": %d, \"character\": %d}, "
          "\"end\": { \"line\": %d, \"character\": %d } }, "
          "\"text\": \"%.*s\" }%s";

      ssize_t num =
          snprintf(NULL, 0, item_fmt, ev->range.begin.line, ev->range.begin.col,
                   ev->range.end.line, ev->range.end.col, escaped.l, escaped.s,
                   i == params->ncontent_changes - 1 ? "" : ", ");

      if (num < 0) {
        return s8("");
      }

      event_buf_size += num;
    } else {
      const char *item_fmt = "{ \"text\", \"%.*s\" }%s";
      ssize_t num = snprintf(NULL, 0, item_fmt, escaped.l, escaped.s,
                             i == params->ncontent_changes - 1 ? "" : ", ");

      if (num < 0) {
        return s8("");
      }

      event_buf_size += num;
    }

    s8delete(escaped);
  }

  ++event_buf_size;
  char *buf = calloc(event_buf_size, 1);
  size_t offset = 0;
  for (size_t i = 0; i < params->ncontent_changes; ++i) {
    struct text_document_content_change_event *ev = &params->content_changes[i];
    struct s8 escaped = escape_json_string(ev->text);
    if (!ev->full_document) {
      const char *item_fmt =
          "{ \"range\": { \"start\": { \"line\": %d, \"character\": %d}, "
          "\"end\": { \"line\": %d, \"character\": %d } }, "
          "\"text\": \"%.*s\" }%s";

      ssize_t num = snprintf(
          &buf[offset], event_buf_size - offset, item_fmt, ev->range.begin.line,
          ev->range.begin.col, ev->range.end.line, ev->range.end.col, escaped.l,
          escaped.s, i == params->ncontent_changes - 1 ? "" : ", ");

      if (num < 0) {
        return s8("");
      }

      offset += num;
    } else {
      const char *item_fmt = "{ \"text\", \"%.*s\" }%s";
      ssize_t num =
          snprintf(&buf[offset], event_buf_size - offset, item_fmt, escaped.l,
                   escaped.s, i == params->ncontent_changes - 1 ? "" : ", ");

      if (num < 0) {
        return s8("");
      }

      offset += num;
    }

    s8delete(escaped);
  }

  const char *fmt =
      "{ \"textDocument\": { \"uri\": \"%.*s\", \"version\": %d }, "
      "\"contentChanges\": [ %s ]"
      "}";

  struct versioned_text_document_identifier *doc = &params->text_document;
  struct s8 json = s8from_fmt(fmt, doc->uri.l, doc->uri.s, doc->version, buf);

  free(buf);
  return json;
}

struct s8 did_open_text_document_params_to_json(
    struct did_open_text_document_params *params) {
  const char *fmt =
      "{ \"textDocument\": { \"uri\": \"%.*s\", \"languageId\": \"%.*s\", "
      "\"version\": %d, \"text\": \"%.*s\" }}";

  struct text_document_item *item = &params->text_document;

  struct s8 escaped_content = escape_json_string(item->text);
  struct s8 json = s8from_fmt(
      fmt, item->uri.l, item->uri.s, item->language_id.l, item->language_id.s,
      item->version, escaped_content.l, escaped_content.s);

  s8delete(escaped_content);
  return json;
}

struct s8 did_save_text_document_params_to_json(
    struct did_save_text_document_params *params) {
  const char *fmt = "{ \"textDocument\": { \"uri\": \"%.*s\" } }";

  struct text_document_identifier *item = &params->text_document;
  struct s8 json = s8from_fmt(fmt, item->uri.l, item->uri.s);
  return json;
}

static struct region parse_region(struct json_object *obj) {
  struct json_object *start = json_get(obj, s8("start"))->value.object;
  struct json_object *end = json_get(obj, s8("end"))->value.object;

  return region_new(
      (struct location){.line = json_get(start, s8("line"))->value.number,
                        .col = json_get(start, s8("character"))->value.number},
      (struct location){.line = json_get(end, s8("line"))->value.number,
                        .col = json_get(end, s8("character"))->value.number});
}

static void parse_diagnostic(uint64_t id, struct json_value *elem,
                             void *userdata) {
  (void)id;
  diagnostic_vec *vec = (diagnostic_vec *)userdata;
  struct json_object *obj = elem->value.object;
  struct json_value *severity = json_get(obj, s8("severity"));
  struct json_value *source = json_get(obj, s8("source"));

  struct diagnostic diag;
  diag.message =
      unescape_json_string(json_get(obj, s8("message"))->value.string);
  diag.region = parse_region(json_get(obj, s8("range"))->value.object);
  diag.severity = severity != NULL
                      ? (enum diagnostic_severity)severity->value.number
                      : LspDiagnostic_Error;
  diag.source = source != NULL ? unescape_json_string(source->value.string)
                               : (struct s8){.l = 0, .s = NULL};

  VEC_PUSH(vec, diag);
}

const char *diag_severity_to_str(enum diagnostic_severity severity) {

  switch (severity) {
  case LspDiagnostic_Error:
    return "error";
  case LspDiagnostic_Warning:
    return "warning";
  case LspDiagnostic_Information:
    return "info";
  case LspDiagnostic_Hint:
    return "hint";
  }

  return "";
}

struct publish_diagnostics_params
diagnostics_from_json(struct json_value *json) {
  struct json_object *obj = json->value.object;
  struct json_value *version = json_get(obj, s8("version"));
  struct publish_diagnostics_params params = {
      .uri = unescape_json_string(json_get(obj, s8("uri"))->value.string),
      .version = version != NULL ? version->value.number : 0,
  };

  struct json_array *diagnostics =
      json_get(obj, s8("diagnostics"))->value.array;
  VEC_INIT(&params.diagnostics, json_array_len(diagnostics));
  json_array_foreach(diagnostics, &params.diagnostics, parse_diagnostic);

  return params;
}

void diagnostic_free(struct diagnostic *diag) {
  s8delete(diag->message);
  s8delete(diag->source);
}

struct s8 document_position_to_json(struct text_document_position *position) {
  const char *fmt = "{ \"textDocument\": { \"uri\": \"%.*s\" }, "
                    "\"position\": { \"line\": %d, \"character\": %d } }";

  struct s8 json = s8from_fmt(fmt, position->uri.l, position->uri.s,
                              position->position.line, position->position.col);
  return json;
}

static struct text_document_location
location_from_json(struct json_value *json) {
  struct text_document_location loc = {0};
  if (json->type != Json_Object) {
    return loc;
  }

  struct json_object *obj = json->value.object;
  loc.uri = unescape_json_string(json_get(obj, s8("uri"))->value.string);
  loc.range = parse_region(json_get(obj, s8("range"))->value.object);

  return loc;
}

static void parse_text_doc_location(uint64_t id, struct json_value *elem,
                                    void *userdata) {
  (void)id;
  location_vec *vec = (location_vec *)userdata;
  VEC_PUSH(vec, location_from_json(elem));
}

struct location_result location_result_from_json(struct json_value *json) {
  if (json->type == Json_Null) {
    return (struct location_result){
        .type = Location_Null,
    };
  } else if (json->type == Json_Object) {
    return (struct location_result){
        .type = Location_Single,
        .location.single = location_from_json(json),
    };
  } else if (json->type == Json_Array) {
    // location link or location
    struct location_result res = {};
    res.type = Location_Array;
    struct json_array *locations = json->value.array;
    VEC_INIT(&res.location.array, json_array_len(locations));
    json_array_foreach(locations, &res.location.array, parse_text_doc_location);
    return res;
  }

  return (struct location_result){.type = Location_Null};
}

void location_result_free(struct location_result *res) {
  switch (res->type) {
  case Location_Null:
    break;
  case Location_Single:
    s8delete(res->location.single.uri);
    break;
  case Location_Array:
    VEC_FOR_EACH(&res->location.array, struct text_document_location * loc) {
      s8delete(loc->uri);
    }
    VEC_DESTROY(&res->location.array);
    break;
  case Location_Link:
    // TODO
    break;
  }
}

static uint32_t severity_to_json(enum diagnostic_severity severity) {
  return (uint32_t)severity;
}

static struct s8 region_to_json(struct region region) {
  const char *fmt = "{ \"start\": { \"line\": %d, \"character\": %d }, "
                    "\"end\": { \"line\": %d, \"character\": %d } }";
  return s8from_fmt(fmt, region.begin.line, region.begin.col, region.end.line,
                    region.end.col);
}

static struct s8 diagnostic_to_json(struct diagnostic *diag) {
  const char *fmt =
      "{ \"range\": %.*s, \"message\": \"%.*s\", \"severity\": %d }";

  struct s8 range = region_to_json(diag->region);
  struct s8 json =
      s8from_fmt(fmt, range.l, range.s, diag->message.l, diag->message.s,
                 severity_to_json(diag->severity));

  s8delete(range);
  return json;
}

static struct s8 diagnostic_vec_to_json(diagnostic_vec diagnostics) {
  size_t ndiags = VEC_SIZE(&diagnostics);
  if (ndiags == 0) {
    return s8new("[]", 2);
  }

  struct s8 *strings = calloc(ndiags, sizeof(struct s8));

  size_t len = 1;
  VEC_FOR_EACH_INDEXED(&diagnostics, struct diagnostic * diag, i) {
    strings[i] = diagnostic_to_json(diag);
    len += strings[i].l + 1;
  }

  uint8_t *final = (uint8_t *)calloc(len, 1);
  struct s8 json = {
      .s = final,
      .l = len,
  };

  final[0] = '[';

  size_t offset = 1;
  for (uint32_t i = 0; i < ndiags; ++i) {
    memcpy(&final[offset], strings[i].s, strings[i].l);
    offset += strings[i].l;

    s8delete(strings[i]);
  }

  final[len - 1] = ']';

  free(strings);

  return json;
}

struct s8 code_action_params_to_json(struct code_action_params *params) {
  const char *fmt = "{ \"textDocument\": { \"uri\": \"%.*s\" }, "
                    " \"range\": %.*s, "
                    " \"context\": { \"diagnostics\": %.*s } }";

  struct s8 json_diags = diagnostic_vec_to_json(params->context.diagnostics);
  struct s8 range = region_to_json(params->range);

  struct s8 json =
      s8from_fmt(fmt, params->text_document.uri.l, params->text_document.uri.s,
                 range.l, range.s, json_diags.l, json_diags.s);

  s8delete(json_diags);
  s8delete(range);
  return json;
}

static struct lsp_command lsp_command_from_json(struct json_value *json) {
  struct json_object *obj = json->value.object;
  struct lsp_command command = {
      .title = unescape_json_string(json_get(obj, s8("title"))->value.string),
      .command =
          unescape_json_string(json_get(obj, s8("command"))->value.string),
      .arguments = s8(""),
  };

  struct json_value *arguments = json_get(obj, s8("arguments"));
  if (arguments != NULL && arguments->type == Json_Array) {
    size_t len = arguments->end - arguments->start;
    command.arguments = s8new((const char *)arguments->start, len);
  }

  return command;
}

static void lsp_action_from_json(uint64_t id, struct json_value *json,
                                 void *userdata) {
  (void)id;
  struct code_actions *actions = (struct code_actions *)userdata;

  struct json_object *obj = json->value.object;
  struct json_value *command_val = json_get(obj, s8("command"));
  if (command_val != NULL && command_val->type == Json_String) {
    VEC_PUSH(&actions->commands, lsp_command_from_json(json));
  } else {
    VEC_APPEND(&actions->code_actions, struct code_action * action);
    action->title =
        unescape_json_string(json_get(obj, s8("title"))->value.string);
    action->kind = s8("");
    action->has_edit = false;
    action->has_command = false;

    struct json_value *kind_val = json_get(obj, s8("kind"));
    if (kind_val != NULL && kind_val->type == Json_String) {
      action->kind = unescape_json_string(kind_val->value.string);
    }

    struct json_value *edit_val = json_get(obj, s8("edit"));
    if (edit_val != NULL && edit_val->type == Json_Object) {
      action->has_edit = true;
      action->edit = workspace_edit_from_json(edit_val);
    }

    command_val = json_get(obj, s8("command"));
    if (command_val != NULL && command_val->type == Json_Object) {
      action->has_command = true;
      action->command = lsp_command_from_json(command_val);
    }
  }
}

struct code_actions lsp_code_actions_from_json(struct json_value *json) {
  struct code_actions actions;

  if (json->type == Json_Array) {
    struct json_array *jcmds = json->value.array;
    VEC_INIT(&actions.commands, json_array_len(jcmds));
    VEC_INIT(&actions.code_actions, json_array_len(jcmds));
    json_array_foreach(jcmds, &actions, lsp_action_from_json);
  } else { /* NULL or wrong type */
    VEC_INIT(&actions.commands, 0);
    VEC_INIT(&actions.code_actions, 0);
  }

  return actions;
}

static void lsp_command_free(struct lsp_command *command) {
  s8delete(command->title);
  s8delete(command->command);

  if (command->arguments.l > 0) {
    s8delete(command->arguments);
  }
}

void lsp_code_actions_free(struct code_actions *actions) {
  VEC_FOR_EACH(&actions->commands, struct lsp_command * command) {
    lsp_command_free(command);
  }

  VEC_DESTROY(&actions->commands);

  VEC_FOR_EACH(&actions->code_actions, struct code_action * action) {
    s8delete(action->title);
    s8delete(action->kind);

    if (action->has_edit) {
      workspace_edit_free(&action->edit);
    }

    if (action->has_command) {
      lsp_command_free(&action->command);
    }
  }

  VEC_DESTROY(&actions->code_actions);
}

struct s8 lsp_command_to_json(struct lsp_command *command) {
  const char *fmt = "{ \"command\": \"%.*s\", \"arguments\": %.*s }";

  return s8from_fmt(fmt, command->command.l, command->command.s,
                    command->arguments.l, command->arguments.s);
}

static void text_edit_from_json(uint64_t id, struct json_value *val,
                                void *userdata) {
  (void)id;
  text_edit_vec *vec = (text_edit_vec *)userdata;
  struct json_object *obj = val->value.object;
  struct text_edit edit = {
      .range = parse_region(json_get(obj, s8("range"))->value.object),
      .new_text =
          unescape_json_string(json_get(obj, s8("newText"))->value.string),
  };
  VEC_PUSH(vec, edit);
}

text_edit_vec text_edits_from_json(struct json_value *json) {
  text_edit_vec vec = {0};

  if (json->type == Json_Array) {
    struct json_array *arr = json->value.array;

    VEC_INIT(&vec, json_array_len(arr));
    json_array_foreach(arr, &vec, text_edit_from_json);
  }

  return vec;
}

static void changes_from_json(struct s8 key, struct json_value *json,
                              void *userdata) {
  change_vec *vec = (change_vec *)userdata;

  struct text_edit_pair pair = {
      .uri = s8dup(key),
  };

  // pick out the edits for this key and create array
  struct json_array *edits = json->value.array;
  VEC_INIT(&pair.edits, json_array_len(edits));
  json_array_foreach(edits, &pair.edits, text_edit_from_json);
  VEC_PUSH(vec, pair);
}

struct workspace_edit workspace_edit_from_json(struct json_value *json) {
  struct workspace_edit edit;
  struct json_object *obj = json->value.object;
  struct json_value *edit_container = json_get(obj, s8("edit"));
  if (edit_container != NULL && edit_container->type == Json_Object) {
    obj = edit_container->value.object;
  }

  struct json_value *changes = json_get(obj, s8("changes"));
  if (changes != NULL) {
    struct json_object *changes_obj = changes->value.object;
    VEC_INIT(&edit.changes, json_len(changes_obj));
    json_foreach(changes_obj, changes_from_json, &edit.changes);
  } else {
    VEC_INIT(&edit.changes, 0);
  }

  return edit;
}

void workspace_edit_free(struct workspace_edit *edit) {
  VEC_FOR_EACH(&edit->changes, struct text_edit_pair * pair) {
    s8delete(pair->uri);
    VEC_FOR_EACH(&pair->edits, struct text_edit * edit) {
      s8delete(edit->new_text);
    }
    VEC_DESTROY(&pair->edits);
  }
  VEC_DESTROY(&edit->changes);
}

uint32_t diag_severity_color(enum diagnostic_severity severity) {
  switch (severity) {
  case LspDiagnostic_Error:
    return Color_BrightRed;
  case LspDiagnostic_Warning:
    return Color_BrightYellow;
  default:
    return Color_BrightBlack;
  }

  return Color_BrightBlack;
}

struct s8
document_formatting_params_to_json(struct document_formatting_params *params) {
  const char *fmt = "{ \"textDocument\": { \"uri\": \"%.*s\" }, \"options\": { "
                    "\"tabSize\": %d, \"insertSpaces\": %s } }";

  return s8from_fmt(fmt, params->text_document.uri.l,
                    params->text_document.uri.s, params->options.tab_size,
                    params->options.use_spaces ? "true" : "false");
}

struct s8 document_range_formatting_params_to_json(
    struct document_range_formatting_params *params) {
  const char *fmt = "{ \"textDocument\": { \"uri\": \"%.*s\" }, \"range\": "
                    "%.*s, \"options\": { "
                    "\"tabSize\": %d, \"insertSpaces\": %s } }";

  struct s8 range = region_to_json(params->range);
  struct s8 json =
      s8from_fmt(fmt, params->text_document.uri.l, params->text_document.uri.s,
                 range.l, range.s, params->options.tab_size,
                 params->options.use_spaces ? "true" : "false");

  s8delete(range);
  return json;
}

void text_edits_free(text_edit_vec edits) {
  VEC_FOR_EACH(&edits, struct text_edit * edit) { s8delete(edit->new_text); }
  VEC_DESTROY(&edits);
}

static void parse_completion_item(uint64_t id, struct json_value *json,
                                  void *userdata) {

  (void)id;
  completions_vec *vec = (completions_vec *)userdata;

  struct json_object *obj = json->value.object;

  struct lsp_completion_item item = {0};
  item.label = s8dup(json_get(obj, s8("label"))->value.string);

  struct json_value *kind_val = json_get(obj, s8("kind"));
  if (kind_val != NULL && kind_val->type == Json_Number) {
    item.kind = (enum completion_item_kind)kind_val->value.number;
  }

  struct json_value *detail_val = json_get(obj, s8("detail"));
  if (detail_val != NULL && detail_val->type == Json_String) {
    item.detail = s8dup(detail_val->value.string);
  }

  struct json_value *sort_txt_val = json_get(obj, s8("sortText"));
  if (sort_txt_val != NULL && sort_txt_val->type == Json_String) {
    item.sort_text = s8dup(sort_txt_val->value.string);
  }

  struct json_value *filter_txt_val = json_get(obj, s8("filterText"));
  if (filter_txt_val != NULL && filter_txt_val->type == Json_String) {
    item.filter_text = s8dup(filter_txt_val->value.string);
  }

  struct json_value *insert_txt_val = json_get(obj, s8("insertText"));
  if (insert_txt_val != NULL && insert_txt_val->type == Json_String) {
    item.insert_text = s8dup(insert_txt_val->value.string);
  }

  // determine type of edit
  struct json_value *edit_val = json_get(obj, s8("textEdit"));
  item.edit_type = TextEdit_None;
  if (edit_val != NULL && edit_val->type == Json_Object) {
    struct json_object *edit_obj = edit_val->value.object;

    struct json_value *insert_val = json_get(edit_obj, s8("insert"));

    if (insert_val != NULL) {
      item.edit_type = TextEdit_InsertReplaceEdit;
      item.edit.insert_replace_edit = (struct insert_replace_edit){
          .insert =
              parse_region(json_get(edit_obj, s8("insert"))->value.object),
          .replace =
              parse_region(json_get(edit_obj, s8("replace"))->value.object),
          .new_text = unescape_json_string(
              json_get(edit_obj, s8("newText"))->value.string),
      };
    } else {
      item.edit_type = TextEdit_TextEdit;
      item.edit.text_edit = (struct text_edit){
          .range = parse_region(json_get(edit_obj, s8("range"))->value.object),
          .new_text = unescape_json_string(
              json_get(edit_obj, s8("newText"))->value.string),
      };
    }
  }

  struct json_value *additional_txt_edits_val =
      json_get(obj, s8("additionalTextEdits"));
  if (additional_txt_edits_val != NULL &&
      additional_txt_edits_val->type == Json_Array) {
    item.additional_text_edits = text_edits_from_json(additional_txt_edits_val);
  }

  struct json_value *command_val = json_get(obj, s8("command"));
  if (command_val != NULL && command_val->type == Json_Object) {
    item.command = lsp_command_from_json(command_val);
  }

  VEC_PUSH(vec, item);
}

struct completion_list completion_list_from_json(struct json_value *json) {

  if (json->type == Json_Null) {
    return (struct completion_list){
        .incomplete = false,
    };
  }

  struct completion_list complist;
  complist.incomplete = false;

  struct json_array *js_items = NULL;
  if (json->type == Json_Object) {
    struct json_object *obj = json->value.object;
    complist.incomplete = json_get(obj, s8("isIncomplete"))->value.boolean;
    js_items = json_get(obj, s8("items"))->value.array;
  } else if (json->type == Json_Array) {
    js_items = json->value.array;
  } else {
    return (struct completion_list){
        .incomplete = false,
    };
  }

  // parse the list
  VEC_INIT(&complist.items, json_array_len(js_items));
  json_array_foreach(js_items, &complist.items, parse_completion_item);

  return complist;
}

void completion_list_free(struct completion_list *complist) {
  VEC_FOR_EACH(&complist->items, struct lsp_completion_item * item) {
    s8delete(item->label);
    s8delete(item->detail);
    s8delete(item->sort_text);
    s8delete(item->filter_text);
    s8delete(item->insert_text);

    if (item->edit_type == TextEdit_TextEdit) {
      s8delete(item->edit.text_edit.new_text);
    } else {
      s8delete(item->edit.insert_replace_edit.new_text);
    }

    text_edits_free(item->additional_text_edits);
    lsp_command_free(&item->command);
  }

  VEC_DESTROY(&complist->items);
}

struct s8 rename_params_to_json(struct rename_params *params) {

  const char *fmt = "{ \"textDocument\": { \"uri\": \"%.*s\" }, "
                    "\"position\": { \"line\": %d, \"character\": %d }, "
                    "\"newName\": \"%.*s\" }";

  struct text_document_position *position = &params->position;
  struct s8 escaped = escape_json_string(params->new_name);
  struct s8 json =
      s8from_fmt(fmt, position->uri.l, position->uri.s, position->position.line,
                 position->position.col, escaped.l, escaped.s);

  s8delete(escaped);
  return json;
}

static void parse_parameter(uint64_t id, struct json_value *json,
                            void *userdata) {

  (void)id;
  param_info_vec *vec = (param_info_vec *)userdata;
  struct json_object *obj = json->value.object;

  struct parameter_information info;
  struct json_value *label = json_get(obj, s8("label"));
  if (label != NULL && label->type == Json_String) {
    info.label = s8dup(label->value.string);
  }

  struct json_value *doc = json_get(obj, s8("documentation"));
  if (doc != NULL && doc->type == Json_String) {
    info.documentation = s8dup(doc->value.string);
  }
  VEC_PUSH(vec, info);
}

static void parse_signature(uint64_t id, struct json_value *json,
                            void *userdata) {

  (void)id;
  signature_info_vec *vec = (signature_info_vec *)userdata;

  struct json_object *obj = json->value.object;

  struct signature_information info;
  struct json_value *label = json_get(obj, s8("label"));
  if (label != NULL && label->type == Json_String) {
    info.label = s8dup(label->value.string);
  }

  struct json_value *doc = json_get(obj, s8("documentation"));
  if (doc != NULL && doc->type == Json_String) {
    info.documentation = s8dup(doc->value.string);
  }

  struct json_value *params = json_get(obj, s8("parameters"));
  if (params != NULL && params->type == Json_Array) {
    struct json_array *arr = params->value.array;
    VEC_INIT(&info.parameters, json_array_len(arr));
    json_array_foreach(arr, &info.parameters, parse_parameter);
  }

  VEC_PUSH(vec, info);
}

struct signature_help signature_help_from_json(struct json_value *value) {
  struct signature_help help = {0};
  struct json_object *obj = value->value.object;

  struct json_value *active_sig = json_get(obj, s8("activeSignature"));
  if (active_sig != NULL && active_sig->type == Json_Number) {
    help.active_signature = active_sig->value.number;
  }

  struct json_value *sigs = json_get(obj, s8("signatures"));
  if (sigs != NULL && sigs->type == Json_Array) {
    struct json_array *arr = sigs->value.array;
    VEC_INIT(&help.signatures, json_array_len(arr));
    json_array_foreach(arr, &help.signatures, parse_signature);
  }

  return help;
}

void signature_help_free(struct signature_help *help) {
  VEC_FOR_EACH(&help->signatures, struct signature_information * info) {
    s8delete(info->label);
    s8delete(info->documentation);

    VEC_FOR_EACH(&info->parameters, struct parameter_information * pinfo) {
      s8delete(pinfo->label);
      s8delete(pinfo->documentation);
    }

    VEC_DESTROY(&info->parameters);
  }

  VEC_DESTROY(&help->signatures);
}

struct hover hover_from_json(struct json_value *value) {
  struct hover hover = {0};
  struct json_object *obj = value->value.object;

  struct json_value *contents = json_get(obj, s8("contents"));
  if (contents != NULL) {
    switch (contents->type) {
    case Json_String:
      hover.contents = unescape_json_string(contents->value.string);
      break;
    case Json_Object: {
      struct json_value *val = json_get(contents->value.object, s8("value"));
      if (val != NULL && val->type == Json_String) {
        hover.contents = unescape_json_string(val->value.string);
      }
    } break;
    default:
      break;
    }
  }

  struct json_value *range = json_get(obj, s8("range"));
  if (range != NULL && range->type == Json_Object) {
    hover.range = parse_region(range->value.object);
  }

  return hover;
}

void hover_free(struct hover *hover) { s8delete(hover->contents); }

struct s8 reference_params_to_json(struct reference_params *params) {
  const char *fmt = "{ \"textDocument\": { \"uri\": \"%.*s\" }, "
                    "\"position\": { \"line\": %d, \"character\": %d }, "
                    "\"includeDeclaration\": \"%s\" }";

  struct text_document_position *position = &params->position;
  struct s8 json = s8from_fmt(fmt, position->uri.l, position->uri.s,
                              position->position.line, position->position.col,
                              params->include_declaration ? "true" : "false");

  return json;
}

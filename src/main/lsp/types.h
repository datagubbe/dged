#ifndef _LSP_TYPES_H
#define _LSP_TYPES_H

#include "dged/json.h"
#include "dged/location.h"
#include "dged/s8.h"
#include "dged/vec.h"

struct buffer;

struct client_capabilities {};

struct workspace_folder {
  struct s8 uri;
  struct s8 name;
};

struct initialize_params {
  int process_id;
  struct client_info {
    struct s8 name;
    struct s8 version;
  } client_info;

  struct client_capabilities client_capabilities;

  struct workspace_folder *workspace_folders;
  size_t nworkspace_folders;
};

enum text_document_sync_kind {
  TextDocumentSync_None = 0,
  TextDocumentSync_Full = 1,
  TextDocumentSync_Incremental = 2,
};

struct text_document_sync {
  enum text_document_sync_kind kind;
  bool open_close;
  bool save;
  bool include_text;
};

enum position_encoding_kind {
  PositionEncoding_Utf8,
  PositionEncoding_Utf16,
  PositionEncoding_Utf32,
};

struct completion_options {
  VEC(struct s8) trigger_characters;
  VEC(struct s8) all_commit_characters;
  bool resolve_provider;
};

struct server_capabilities {
  struct text_document_sync text_document_sync;
  enum position_encoding_kind position_encoding;
  bool supports_completion;
  struct completion_options completion_options;
};

struct initialize_result {
  struct server_capabilities capabilities;
  struct server_info {
    struct s8 name;
    struct s8 version;
  } server_info;
};

struct s8 initialize_params_to_json(struct initialize_params *params);
struct initialize_result initialize_result_from_json(struct json_value *json);
void initialize_result_free(struct initialize_result *);
struct s8 position_encoding_kind_str(enum position_encoding_kind);

struct text_document_item {
  struct s8 uri;
  struct s8 language_id;
  uint64_t version;
  struct s8 text;
};

struct text_document_identifier {
  struct s8 uri;
};

struct text_document_position {
  struct s8 uri;
  struct location position;
};

struct text_document_location {
  struct s8 uri;
  struct region range;
};

struct versioned_text_document_identifier {
  struct s8 uri;
  uint64_t version;
};

struct did_open_text_document_params {
  struct text_document_item text_document;
};

enum location_type {
  Location_Single,
  Location_Array,
  Location_Link,
  Location_Null,
};

typedef VEC(struct text_document_location) location_vec;

struct location_result {
  enum location_type type;
  union location_data {
    struct text_document_location single;
    location_vec array;
  } location;
};

struct did_change_text_document_params {
  struct versioned_text_document_identifier text_document;
  struct text_document_content_change_event *content_changes;
  size_t ncontent_changes;
};

struct did_save_text_document_params {
  struct text_document_identifier text_document;
  struct s8 text;
};

struct text_document_content_change_event {
  struct region range;
  struct s8 text;
  bool full_document;
};

enum diagnostic_severity {
  LspDiagnostic_Error = 1,
  LspDiagnostic_Warning = 2,
  LspDiagnostic_Information = 3,
  LspDiagnostic_Hint = 4,
};

struct diagnostic {
  struct s8 message;
  struct s8 source;
  struct region region;
  enum diagnostic_severity severity;
};

typedef VEC(struct diagnostic) diagnostic_vec;

struct publish_diagnostics_params {
  struct s8 uri;
  uint64_t version;
  diagnostic_vec diagnostics;
};

struct code_action_context {
  diagnostic_vec diagnostics;
};

struct code_action_params {
  struct text_document_identifier text_document;
  struct region range;
  struct code_action_context context;
};

struct text_edit {
  struct region range;
  struct s8 new_text;
};

typedef VEC(struct text_edit) text_edit_vec;

struct text_edit_pair {
  struct s8 uri;
  text_edit_vec edits;
};

typedef VEC(struct text_edit_pair) change_vec;

struct text_document_edit {
  struct versioned_text_document_identifier text_document;
  text_edit_vec edits;
};

typedef VEC(struct text_document_edit) text_document_edit_vec;

struct workspace_edit {
  change_vec changes;
  text_document_edit_vec document_changes;
};

struct lsp_command {
  struct s8 title;
  struct s8 command;
  struct s8 arguments;
};

struct code_action {
  struct s8 title;
  struct s8 kind;

  bool has_edit;
  struct workspace_edit edit;

  bool has_command;
  struct lsp_command command;
};

typedef VEC(struct lsp_command) lsp_command_vec;
typedef VEC(struct code_action) code_action_vec;

struct code_actions {
  lsp_command_vec commands;
  code_action_vec code_actions;
};

struct formatting_options {
  size_t tab_size;
  bool use_spaces;
};

struct document_formatting_params {
  struct text_document_identifier text_document;
  struct formatting_options options;
};

struct document_range_formatting_params {
  struct text_document_identifier text_document;
  struct region range;
  struct formatting_options options;
};

enum completion_item_kind {
  CompletionItem_Text = 1,
  CompletionItem_Method = 2,
  CompletionItem_Function = 3,
  CompletionItem_Constructor = 4,
  CompletionItem_Field = 5,
  CompletionItem_Variable = 6,
  CompletionItem_Class = 7,
  CompletionItem_Interface = 8,
  CompletionItem_Module = 9,
  CompletionItem_Property = 10,
  CompletionItem_Unit = 11,
  CompletionItem_Value = 12,
  CompletionItem_Enum = 13,
  CompletionItem_Keyword = 14,
  CompletionItem_Snippet = 15,
  CompletionItem_Color = 16,
  CompletionItem_File = 17,
  CompletionItem_Reference = 18,
  CompletionItem_Folder = 19,
  CompletionItem_EnumMember = 20,
  CompletionItem_Constant = 21,
  CompletionItem_Struct = 22,
  CompletionItem_Event = 23,
  CompletionItem_Operator = 24,
  CompletionItem_TypeParameter = 25,
};

enum text_edit_type {
  TextEdit_None,
  TextEdit_TextEdit,
  TextEdit_InsertReplaceEdit,
};

struct insert_replace_edit {
  struct s8 new_text;
  struct region insert;
  struct region replace;
};

struct lsp_completion_item {
  struct s8 label;
  enum completion_item_kind kind;
  struct s8 detail;
  struct s8 sort_text;
  struct s8 filter_text;
  struct s8 insert_text;

  enum text_edit_type edit_type;
  union edit_ {
    struct text_edit text_edit;
    struct insert_replace_edit insert_replace_edit;
  } edit;

  text_edit_vec additional_text_edits;

  struct lsp_command command;
};

typedef VEC(struct lsp_completion_item) completions_vec;

struct completion_list {
  bool incomplete;
  completions_vec items;
};

struct rename_params {
  struct text_document_position position;
  struct s8 new_name;
};

struct parameter_information {
  struct s8 label;
  struct s8 documentation;
};

typedef VEC(struct parameter_information) param_info_vec;

struct signature_information {
  struct s8 label;
  struct s8 documentation;
  param_info_vec parameters;
};

typedef VEC(struct signature_information) signature_info_vec;

struct signature_help {
  uint32_t active_signature;
  signature_info_vec signatures;
};

struct hover {
  struct s8 contents;
  struct region range;
};

struct reference_params {
  struct text_document_position position;
  bool include_declaration;
};

struct text_document_item text_document_item_from_buffer(struct buffer *buffer);
struct versioned_text_document_identifier
versioned_identifier_from_buffer(struct buffer *buffer);

void versioned_text_document_identifier_free(
    struct versioned_text_document_identifier *);
void text_document_item_free(struct text_document_item *);

struct s8 did_change_text_document_params_to_json(
    struct did_change_text_document_params *);
struct s8
did_open_text_document_params_to_json(struct did_open_text_document_params *);
struct s8
did_save_text_document_params_to_json(struct did_save_text_document_params *);

struct publish_diagnostics_params
diagnostics_from_json(struct json_value *json);

const char *diag_severity_to_str(enum diagnostic_severity severity);
uint32_t diag_severity_color(enum diagnostic_severity severity);
void diagnostic_free(struct diagnostic *);

struct s8 document_position_to_json(struct text_document_position *position);
struct location_result location_result_from_json(struct json_value *json);
void location_result_free(struct location_result *res);

struct s8 code_action_params_to_json(struct code_action_params *);

struct code_actions lsp_code_actions_from_json(struct json_value *);
void lsp_code_actions_free(struct code_actions *);
struct s8 lsp_command_to_json(struct lsp_command *);

text_edit_vec text_edits_from_json(struct json_value *);
void text_edits_free(text_edit_vec);
struct workspace_edit workspace_edit_from_json(struct json_value *);
void workspace_edit_free(struct workspace_edit *);

struct s8
document_formatting_params_to_json(struct document_formatting_params *);
struct s8 document_range_formatting_params_to_json(
    struct document_range_formatting_params *);

struct completion_list completion_list_from_json(struct json_value *);
void completion_list_free(struct completion_list *);

struct s8 rename_params_to_json(struct rename_params *);

struct signature_help signature_help_from_json(struct json_value *);
void signature_help_free(struct signature_help *);

struct hover hover_from_json(struct json_value *);
void hover_free(struct hover *);

struct s8 reference_params_to_json(struct reference_params *);

#endif

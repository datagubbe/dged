#include "completion.h"

#include <stddef.h>
#include <stdlib.h>

#include "completion/matchers.h"
#include "dged/s8.h"
#include "dged/vec.h"
#include "types.h"

#include "dged/buffer.h"
#include "dged/buffer_view.h"
#include "dged/minibuffer.h"
#include "main/completion.h"
#include "main/lsp.h"

struct completion_ctx {
  struct lsp_server *server;
  struct completion_context comp_ctx;
  struct completion_list completions;
  struct s8 cached_with;
  uint64_t last_request;

  triggerchar_vec trigger_chars;
};

struct symbol {
  struct s8 symbol;
  struct region region;
};

static struct symbol current_symbol(struct buffer *buffer, struct location at) {
  // use previous char here since the cursor has moved after typing
  struct region word = buffer_word_at(buffer, buffer_previous_char(buffer, at));
  if (!region_has_size(word)) {
    return (struct symbol){
        .symbol =
            (struct s8){
                .s = NULL,
                .l = 0,
            },
        .region = word,
    };
  };

  struct text_chunk line = buffer_region(buffer, region_new(word.begin, at));
  struct s8 symbol = s8new((const char *)line.text, line.nbytes);

  if (line.allocated) {
    free(line.text);
  }

  return (struct symbol){.symbol = symbol, .region = word};
}

struct completion_ctx *create_completion_ctx(struct lsp_server *server,
                                             triggerchar_vec *trigger_chars) {
  struct completion_ctx *ctx =
      (struct completion_ctx *)calloc(1, sizeof(struct completion_ctx));

  ctx->server = server;
  ctx->completions.incomplete = false;
  ctx->cached_with.s = NULL;
  ctx->cached_with.l = 0;
  VEC_INIT_EMPTY(&ctx->completions.items);

  VEC_INIT(&ctx->trigger_chars, VEC_SIZE(trigger_chars));
  VEC_FOR_EACH(trigger_chars, struct s8 * s) {
    VEC_PUSH(&ctx->trigger_chars, s8dup(*s));
  }

  return ctx;
}

void destroy_completion_ctx(struct completion_ctx *ctx) {
  completion_list_free(&ctx->completions);

  s8delete(ctx->cached_with);

  VEC_FOR_EACH(&ctx->trigger_chars, struct s8 * s) { s8delete(*s); }
  VEC_DESTROY(&ctx->trigger_chars);

  free(ctx);
}

static char *item_kind_to_str(enum completion_item_kind kind) {
  switch (kind) {
  case CompletionItem_Text:
    return "tx";

  case CompletionItem_Method:
    return "mth";

  case CompletionItem_Function:
    return "fn";

  case CompletionItem_Constructor:
    return "cons";

  case CompletionItem_Field:
    return "field";

  case CompletionItem_Variable:
    return "var";

  case CompletionItem_Class:
    return "cls";

  case CompletionItem_Interface:
    return "iface";

  case CompletionItem_Module:
    return "mod";

  case CompletionItem_Property:
    return "prop";

  case CompletionItem_Unit:
    return "unit";

  case CompletionItem_Value:
    return "val";

  case CompletionItem_Enum:
    return "enum";

  case CompletionItem_Keyword:
    return "kw";

  case CompletionItem_Snippet:
    return "snp";

  case CompletionItem_Color:
    return "col";

  case CompletionItem_File:
    return "file";

  case CompletionItem_Reference:
    return "ref";

  case CompletionItem_Folder:
    return "fld";

  case CompletionItem_EnumMember:
    return "em";

  case CompletionItem_Constant:
    return "const";

  case CompletionItem_Struct:
    return "struct";

  case CompletionItem_Event:
    return "ev";

  case CompletionItem_Operator:
    return "op";

  case CompletionItem_TypeParameter:
    return "tp";

  default:
    return "";
  }
}

static struct region lsp_item_render(void *data, struct buffer *buffer) {
  struct lsp_completion_item *item = (struct lsp_completion_item *)data;
  struct location begin = buffer_end(buffer);
  struct s8 kind_str = s8from_fmt("(%s)", item_kind_to_str(item->kind));
  struct s8 txt = s8from_fmt("%-8.*s%.*s", kind_str.l, kind_str.s,
                             item->label.l, item->label.s);
  struct location end = buffer_add(buffer, begin, txt.s, txt.l);
  s8delete(txt);
  s8delete(kind_str);

  return region_new(begin, end);
}

static void lsp_item_selected(void *data, struct buffer_view *view) {
  struct lsp_completion_item *item = (struct lsp_completion_item *)data;
  struct buffer *buffer = view->buffer;
  struct lsp_server *lsp_server = lsp_server_for_buffer(buffer);

  abort_completion();

  if (lsp_server == NULL) {
    return;
  }

  switch (item->edit_type) {
  case TextEdit_None: {
    struct symbol symbol = current_symbol(buffer, view->dot);
    struct s8 insert = item->insert_text;

    // FIXME: why does this happen?
    if (symbol.symbol.l >= insert.l) {
      s8delete(symbol.symbol);
      return;
    }

    if (symbol.symbol.l > 0) {
      insert.s += symbol.symbol.l;
      insert.l -= symbol.symbol.l;
    }

    s8delete(symbol.symbol);

    buffer_push_undo_boundary(buffer);
    struct location at = buffer_add(buffer, view->dot, insert.s, insert.l);
    buffer_view_goto(view, at);

  } break;
  case TextEdit_TextEdit: {
    struct text_edit *ed = &item->edit.text_edit;
    struct region reg = lsp_range_to_coordinates(lsp_server, buffer, ed->range);
    struct location at = reg.begin;
    if (!region_is_inside(reg, view->dot)) {
      reg.end = view->dot;
    }

    if (region_has_size(reg)) {
      at = buffer_delete(buffer, reg);
    }

    buffer_push_undo_boundary(buffer);
    at = buffer_add(buffer, at, ed->new_text.s, ed->new_text.l);
    buffer_view_goto(view, at);
  } break;

  case TextEdit_InsertReplaceEdit: {
    struct insert_replace_edit *ed = &item->edit.insert_replace_edit;
    struct region reg =
        lsp_range_to_coordinates(lsp_server, buffer, ed->replace);

    if (!region_is_inside(reg, view->dot)) {
      reg.end = view->dot;
    }

    if (region_has_size(reg)) {
      buffer_delete(buffer, reg);
    }

    buffer_push_undo_boundary(buffer);
    struct location at =
        buffer_add(buffer, ed->insert.begin, ed->new_text.s, ed->new_text.l);
    buffer_view_goto(view, at);
  } break;
  }

  if (!VEC_EMPTY(&item->additional_text_edits)) {
    apply_edits_buffer(lsp_server, view->buffer, item->additional_text_edits,
                       &view->dot);
  }
}

static void lsp_item_cleanup(void *data) { (void)data; }

static struct s8 get_filter_text(struct lsp_completion_item *item) {
  return item->filter_text.l > 0 ? item->filter_text : item->label;
}

static struct s8 get_sort_text(struct lsp_completion_item *item) {
  return item->sort_text.l > 0 ? item->sort_text : item->label;
}

static int compare_lsp_items(const void *item1, const void *item2) {
  struct s8 sort1 = get_sort_text(
      (struct lsp_completion_item *)((struct completion *)item1)->data);
  struct s8 sort2 = get_sort_text(
      (struct lsp_completion_item *)((struct completion *)item2)->data);

  return s8cmp(sort1, sort2);
}

static void fill_completions(struct completion_ctx *lsp_ctx, struct s8 needle) {
  if (VEC_EMPTY(&lsp_ctx->completions.items)) {
    return;
  }

  VEC(struct completion) completions;
  VEC_INIT(&completions, 16);

  VEC_FOR_EACH(&lsp_ctx->completions.items,
               struct lsp_completion_item * lsp_item) {

    size_t match_begin, match_end;
    uint32_t score;
    if (filter_exact(needle, get_filter_text(lsp_item), &match_begin,
                     &match_end, &score)) {
      VEC_APPEND(&completions, struct completion * c);

      c->data = lsp_item;
      c->render = lsp_item_render;
      c->selected = lsp_item_selected;
      c->cleanup = lsp_item_cleanup;
    }
  }

  qsort(VEC_ENTRIES(&completions), VEC_SIZE(&completions),
        sizeof(struct completion), compare_lsp_items);

  lsp_ctx->comp_ctx.add_completions(VEC_ENTRIES(&completions),
                                    VEC_SIZE(&completions));
  VEC_DESTROY(&completions);
}

static void handle_completion_response(struct lsp_server *server,
                                       struct lsp_response *response,
                                       void *userdata) {
  (void)server;
  struct completion_ctx *lsp_ctx = (struct completion_ctx *)userdata;

  if (response->id != lsp_ctx->last_request) {
    // discard any old requests
    return;
  }

  completion_list_free(&lsp_ctx->completions);
  lsp_ctx->completions = completion_list_from_json(&response->value.result);

  fill_completions(lsp_ctx, lsp_ctx->cached_with);
}

static void complete_with_lsp(struct completion_context ctx, bool deletion,
                              void *userdata) {
  (void)deletion;
  struct completion_ctx *lsp_ctx = (struct completion_ctx *)userdata;
  lsp_ctx->comp_ctx = ctx;

  struct symbol sym = current_symbol(ctx.buffer, ctx.location);
  struct s8 symbol = sym.symbol;

  // check if the symbol is too short for triggering completion
  bool should_activate =
      (symbol.l >= 3 || completion_active()) && !s8onlyws(symbol);

  // use trigger chars as an alternative activation condition
  if (!should_activate) {
    struct location begin = buffer_previous_char(ctx.buffer, ctx.location);
    struct location end = begin;
    end.col += 4;
    struct text_chunk txt = buffer_region(ctx.buffer, region_new(begin, end));
    struct s8 t = {
        .s = txt.text,
        .l = txt.nbytes,
    };

    VEC_FOR_EACH(&lsp_ctx->trigger_chars, struct s8 * tc) {
      if (s8startswith(t, *tc)) {
        should_activate = true;
        goto done;
      }
    }
  done:
    if (txt.allocated) {
      free(txt.text);
    }
  }

  // if we still should not activate, we give up
  if (!should_activate) {
    s8delete(symbol);
    return;
  }

  bool has_completions = !VEC_EMPTY(&lsp_ctx->completions.items);
  if (completion_active() && has_completions &&
      !lsp_ctx->completions.incomplete && !s8empty(lsp_ctx->cached_with) &&
      s8startswith(symbol, lsp_ctx->cached_with)) {
    fill_completions(lsp_ctx, symbol);
  } else {
    uint64_t id = new_pending_request(lsp_ctx->server,
                                      handle_completion_response, lsp_ctx);
    lsp_ctx->last_request = id;

    struct versioned_text_document_identifier doc =
        versioned_identifier_from_buffer(ctx.buffer);
    struct text_document_position params = {
        .uri = doc.uri,
        .position = ctx.location,
    };

    s8delete(lsp_ctx->cached_with);
    lsp_ctx->cached_with = s8dup(symbol);

    struct s8 json_payload = document_position_to_json(&params);
    lsp_send(
        lsp_backend(lsp_ctx->server),
        lsp_create_request(id, s8("textDocument/completion"), json_payload));

    versioned_text_document_identifier_free(&doc);
    s8delete(json_payload);
  }

  s8delete(symbol);
}

void enable_completion_for_buffer(struct completion_ctx *ctx,
                                  struct buffer *buffer) {
  struct completion_provider prov = {
      .name = "lsp",
      .complete = complete_with_lsp,
      .userdata = ctx,
  };
  struct completion_provider providers[] = {prov};

  add_completion_providers(buffer, providers, 1);
}

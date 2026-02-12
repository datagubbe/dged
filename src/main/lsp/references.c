#include "references.h"

#include "dged/binding.h"
#include "dged/buffer.h"
#include "dged/buffer_view.h"
#include "dged/buffers.h"
#include "dged/command.h"
#include "dged/display.h"
#include "dged/location.h"
#include "dged/minibuffer.h"
#include "dged/path.h"
#include "dged/s8.h"
#include "dged/text.h"
#include "dged/vec.h"
#include "dged/window.h"

#include "bindings.h"
#include "lsp.h"
#include "lsp/goto.h"
#include "lsp/types.h"

struct link {
  struct s8 uri;
  struct region target_region;
  struct region region;
};

typedef VEC(struct link) link_vec;

static link_vec g_links;
static struct buffer *g_prev_buffer = NULL;
static struct location g_prev_location;

static int32_t references_close(struct command_ctx ctx, int argc,
                                const char **argv) {
  (void)argc;
  (void)argv;

  if (g_prev_buffer != NULL) {
    // validate that it is still a valid buffer
    struct buffer *b =
        buffers_find_by_filename(ctx.buffers, g_prev_buffer->filename);
    window_set_buffer(ctx.active_window, b);
    buffer_view_goto(window_buffer_view(ctx.active_window), g_prev_location);
  } else if (window_has_prev_buffer_view(ctx.active_window)) {
    window_set_buffer(ctx.active_window,
                      window_prev_buffer_view(ctx.active_window)->buffer);
  } else {
    minibuffer_echo_timeout(4, "no previous buffer to go to");
  }

  return 0;
}

static int32_t references_visit(struct command_ctx ctx, int argc,
                                const char **argv) {

  (void)argc;
  (void)argv;

  struct buffer_view *view = window_buffer_view(ctx.active_window);

  VEC_FOR_EACH(&g_links, struct link * link) {
    if (region_is_inside(link->region, view->dot)) {
      lsp_jump_to(
          (struct text_document_location){
              .range = link->target_region,
              .uri = link->uri,
          },
          NULL);

      return 0;
    }
  }

  return 0;
}

static void reference_buffer_closed(struct buffer *buffer, void *userdata) {
  (void)buffer;
  link_vec *vec = (link_vec *)userdata;
  VEC_FOR_EACH(vec, struct link * link) { s8delete(link->uri); }
  VEC_DESTROY(vec);
}

static void handle_references_response(struct lsp_server *server,
                                       struct lsp_response *response,
                                       void *userdata) {
  (void)server;

  struct buffers *buffers = (struct buffers *)userdata;
  if (response->value.result.type == Json_Null) {
    minibuffer_echo_timeout(4, "references: no references found");
    return;
  }

  struct location_result locations =
      location_result_from_json(&response->value.result);

  if (locations.type != Location_Array) {
    minibuffer_echo_timeout(4, "references: expected location array");
    return;
  }

  struct buffer *b = buffers_find(buffers, "*lsp-references*");
  if (b == NULL) {
    b = buffers_add(buffers, buffer_create("*lsp-references*"));
    b->lazy_row_add = false;
    b->retain_properties = true;
    static struct command ref_close = {
        .name = "ref_close",
        .fn = references_close,
    };

    static struct command ref_visit_cmd = {
        .name = "ref_visit",
        .fn = references_visit,
    };

    struct binding bindings[] = {
        ANONYMOUS_BINDING(None, 'q', &ref_close),
        ANONYMOUS_BINDING(ENTER, &ref_visit_cmd),
        ANONYMOUS_BINDING(NUMPAD_ENTER, &ref_visit_cmd),
    };
    struct keymap km = keymap_create("references", 2);
    keymap_bind_keys(&km, bindings, sizeof(bindings) / sizeof(bindings[0]));
    buffer_add_keymap(b, km);
    buffer_add_destroy_hook(b, reference_buffer_closed, &g_links);
    VEC_INIT(&g_links, 16);
  }

  buffer_set_readonly(b, false);
  buffer_clear(b);

  VEC_FOR_EACH(&g_links, struct link * link) { s8delete(link->uri); }
  VEC_CLEAR(&g_links);

  buffer_clear_text_properties(b);
  VEC_FOR_EACH(&locations.location.array, struct text_document_location * loc) {
    uint32_t found = 0, found_at = (uint32_t)-1;
    for (uint32_t i = 0; i < loc->uri.l; ++i) {
      uint8_t b = loc->uri.s[i];
      if (b == ':') {
        ++found;
      } else if (b == '/' && found == 1) {
        ++found;
      } else if (b == '/' && found == 2) {
        found_at = i;
      } else {
        found = 0;
      }
    }

    struct s8 path = loc->uri;
    if (found_at != (uint32_t)-1) {
      path.s += found_at;
      path.l -= found_at;
    }

    struct s8 cwd = working_dir();
    struct s8 relpath = relative_to(path, cwd);
    s8delete(cwd);

    struct location start = buffer_end(b);
    struct location fileend = buffer_add(b, start, relpath.s, relpath.l);
    s8delete(relpath);
    buffer_add_text_property(b, start,
                             (struct location){
                                 .line = fileend.line,
                                 .col = fileend.col > 0 ? fileend.col - 1 : 0,
                             },
                             (struct text_property){
                                 .type = TextProperty_Colors,
                                 .data.colors =
                                     (struct text_property_colors){
                                         .set_bg = false,
                                         .set_fg = true,
                                         .fg = Color_Magenta,
                                         .underline = true,
                                     },
                             });

    struct s8 line = s8from_fmt(":%d", loc->range.begin.line);
    struct location end = buffer_add(b, buffer_end(b), line.s, line.l);
    s8delete(line);

    VEC_PUSH(&g_links, ((struct link){
                           .target_region = loc->range,
                           .region = region_new(start, end),
                           .uri = s8dup(loc->uri),
                       }));

    buffer_newline(b, end);
  }

  buffer_set_readonly(b, true);

  if (window_find_by_buffer(b) == NULL) {
    struct window *w = windows_get_active();
    g_prev_buffer = window_buffer(w);
    g_prev_location = window_buffer_view(w)->dot;
    window_set_buffer(w, b);
  }
  location_result_free(&locations);
}

void lsp_references(struct lsp_server *server, struct buffer *buffer,
                    struct location at, struct buffers *buffers) {
  uint64_t id =
      new_pending_request(server, handle_references_response, buffers);
  struct versioned_text_document_identifier doc =
      versioned_identifier_from_buffer(buffer);

  struct reference_params params = {
      .position =
          {
              .uri = doc.uri,
              .position = at,
          },
      .include_declaration = true,
  };

  struct s8 json_payload = reference_params_to_json(&params);
  lsp_send(lsp_backend(server),
           lsp_create_request(id, s8("textDocument/references"), json_payload));

  versioned_text_document_identifier_free(&doc);
  s8delete(json_payload);
}

int32_t lsp_references_cmd(struct command_ctx ctx, int argc,
                           const char **argv) {
  (void)argc;
  (void)argv;

  struct buffer_view *bv = window_buffer_view(ctx.active_window);
  struct lsp_server *server = lsp_server_for_lang_id(bv->buffer->lang.id);
  if (!lsp_server_active(server)) {
    minibuffer_echo_timeout(4, "no working lsp server associated with %s",
                            bv->buffer->name);
    return 0;
  }

  lsp_references(server, bv->buffer, bv->dot, ctx.buffers);
  return 0;
}

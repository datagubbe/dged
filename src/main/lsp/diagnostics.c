#include "diagnostics.h"

#include <stdio.h>

#include "dged/binding.h"
#include "dged/buffer.h"
#include "dged/buffer_view.h"
#include "dged/buffers.h"
#include "dged/location.h"
#include "dged/lsp.h"
#include "dged/minibuffer.h"
#include "dged/vec.h"
#include "main/bindings.h"
#include "main/lsp.h"

#define DIAGNOSTIC_BUFNAME "*lsp-diagnostics*"

typedef VEC(struct lsp_buffer_diagnostics) buffer_diagnostics_vec;

struct lsp_diagnostics {
  buffer_diagnostics_vec buffer_diagnostics;
};

struct diagnostic_region {
  struct diagnostic *diagnostic;
  struct region region;
};

struct active_diagnostics {
  struct buffer *buffer;
  VEC(struct diagnostic_region) diag_regions;
};

static struct active_diagnostics g_active_diagnostic;

static struct s8 diagnostics_modeline(struct buffer_view *view,
                                      void *userdata) {
  struct lsp_diagnostics *diag = (struct lsp_diagnostics *)userdata;

  struct lsp_buffer_diagnostics *diags =
      diagnostics_for_buffer(diag, view->buffer);

  size_t nerrs = 0, nwarn = 0;
  if (diags != NULL) {
    VEC_FOR_EACH(&diags->diagnostics, struct diagnostic * d) {
      if (d->severity == LspDiagnostic_Error) {
        ++nerrs;
      } else if (d->severity == LspDiagnostic_Warning) {
        ++nwarn;
      }
    }

    return s8from_fmt("E: %d, W: %d", nerrs, nwarn);
  }

  return s8("");
}

struct lsp_diagnostics *diagnostics_create(void) {
  struct lsp_diagnostics *d = calloc(1, sizeof(struct lsp_diagnostics));

  VEC_INIT(&d->buffer_diagnostics, 16);
  buffer_view_add_modeline_hook(diagnostics_modeline, d);
  VEC_INIT(&g_active_diagnostic.diag_regions, 0);
  g_active_diagnostic.buffer = NULL;
  return d;
}

void diagnostics_destroy(struct lsp_diagnostics *d) {
  VEC_FOR_EACH(&d->buffer_diagnostics, struct lsp_buffer_diagnostics * diag) {
    VEC_FOR_EACH(&diag->diagnostics, struct diagnostic * d) {
      diagnostic_free(d);
    }
    VEC_DESTROY(&diag->diagnostics);
  }

  VEC_DESTROY(&d->buffer_diagnostics);
  VEC_DESTROY(&g_active_diagnostic.diag_regions);
  free(d);
}

struct lsp_buffer_diagnostics *diagnostics_for_buffer(struct lsp_diagnostics *d,
                                                      struct buffer *buffer) {
  VEC_FOR_EACH(&d->buffer_diagnostics, struct lsp_buffer_diagnostics * diag) {
    if (diag->buffer == buffer) {
      return diag;
    }
  }

  return NULL;
}

static int32_t diagnostics_goto_fn(struct command_ctx ctx, int argc,
                                   const char **argv) {
  (void)argc;
  (void)argv;

  struct buffer *db = buffers_find(ctx.buffers, DIAGNOSTIC_BUFNAME);
  if (db == NULL) {
    return 0;
  }

  struct window *w = window_find_by_buffer(db);
  if (w == NULL) {
    return 0;
  }

  if (g_active_diagnostic.buffer == NULL) {
    return 0;
  }

  struct buffer_view *bv = window_buffer_view(w);

  VEC_FOR_EACH(&g_active_diagnostic.diag_regions,
               struct diagnostic_region * reg) {
    if (region_is_inside(reg->region, bv->dot)) {
      struct window *target_win =
          window_find_by_buffer(g_active_diagnostic.buffer);
      if (target_win == NULL) {
        // if the buffer is not open, reuse the diagnostic buffer
        target_win = w;
        window_set_buffer(target_win, g_active_diagnostic.buffer);
      }

      buffer_view_goto(window_buffer_view(target_win),
                       reg->diagnostic->region.begin);
      windows_set_active(target_win);
      return 0;
    }
  }

  return 0;
}

static int32_t diagnostics_close_fn(struct command_ctx ctx, int argc,
                                    const char **argv) {
  (void)argc;
  (void)argv;

  if (window_has_prev_buffer_view(ctx.active_window)) {
    window_set_buffer(ctx.active_window,
                      window_prev_buffer_view(ctx.active_window)->buffer);
  }

  return 0;
}

static struct buffer *update_diagnostics_buffer(struct lsp_server *server,
                                                struct buffers *buffers,
                                                diagnostic_vec diagnostics,
                                                struct buffer *buffer) {
  char buf[2048];
  struct buffer *db = buffers_find(buffers, DIAGNOSTIC_BUFNAME);
  if (db == NULL) {
    struct buffer buf = buffer_create(DIAGNOSTIC_BUFNAME);
    buf.lazy_row_add = false;
    buf.retain_properties = true;
    db = buffers_add(buffers, buf);

    static struct command diagnostics_goto = {
        .name = "diagnostics-goto",
        .fn = diagnostics_goto_fn,
    };

    static struct command diagnostics_close = {
        .name = "diagnostics-close",
        .fn = diagnostics_close_fn,
    };

    struct binding bindings[] = {
        ANONYMOUS_BINDING(ENTER, &diagnostics_goto),
        ANONYMOUS_BINDING(NUMPAD_ENTER, &diagnostics_goto),
        ANONYMOUS_BINDING(None, 'q', &diagnostics_close),
    };
    struct keymap km = keymap_create("diagnostics", 8);
    keymap_bind_keys(&km, bindings, sizeof(bindings) / sizeof(bindings[0]));
    buffer_add_keymap(db, km);
  }
  buffer_set_readonly(db, false);
  buffer_clear(db);
  buffer_clear_text_properties(db);

  g_active_diagnostic.buffer = buffer;
  ssize_t len = snprintf(buf, 2048, "Diagnostics for %s:\n\n", buffer->name);
  if (len != -1) {
    buffer_add(db, buffer_end(db), (uint8_t *)buf, len);
    buffer_add_text_property(
        db, (struct location){.line = 0, .col = 0},
        (struct location){.line = 1, .col = 0},
        (struct text_property){.type = TextProperty_Colors,
                               .data.colors.underline = true});
  }

  VEC_DESTROY(&g_active_diagnostic.diag_regions);
  VEC_INIT(&g_active_diagnostic.diag_regions, VEC_SIZE(&diagnostics));
  VEC_FOR_EACH(&diagnostics, struct diagnostic * diag) {
    struct location start = buffer_end(db);
    char src[128];
    size_t srclen = snprintf(src, 128, "%.*s%s", diag->source.l, diag->source.s,
                             diag->source.l > 0 ? ": " : "");
    const char *severity_str = diag_severity_to_str(diag->severity);
    size_t severity_str_len = strlen(severity_str);
    struct region reg = lsp_range_to_coordinates(server, buffer, diag->region);
    len = snprintf(buf, 2048,
                   "%s%s [%d, %d]: %.*s\n-------------------------------", src,
                   severity_str, reg.begin.line + 1, reg.begin.col,
                   diag->message.l, diag->message.s);

    if (len != -1) {
      buffer_add(db, buffer_end(db), (uint8_t *)buf, len);

      struct location srcend = start;
      srcend.col += srclen - 3;
      buffer_add_text_property(
          db, start, srcend,
          (struct text_property){.type = TextProperty_Colors,
                                 .data.colors.underline = true});

      uint32_t color = diag_severity_color(diag->severity);
      struct location sevstart = start;
      sevstart.col += srclen;
      struct location sevend = sevstart;
      sevend.col += severity_str_len;
      buffer_add_text_property(
          db, sevstart, sevend,
          (struct text_property){.type = TextProperty_Colors,
                                 .data.colors.set_fg = true,
                                 .data.colors.fg = color});

      VEC_PUSH(&g_active_diagnostic.diag_regions,
               ((struct diagnostic_region){
                   .diagnostic = diag,
                   .region = region_new(start, buffer_end(db)),
               }));

      buffer_newline(db, buffer_end(db));
    }
  }

  buffer_set_readonly(db, true);
  return db;
}

void handle_publish_diagnostics(struct lsp_server *server,
                                struct buffers *buffers,
                                struct lsp_notification *notification) {
  struct publish_diagnostics_params params =
      diagnostics_from_json(&notification->params);
  if (s8startswith(params.uri, s8("file://"))) {
    const char *p = s8tocstr(params.uri);
    struct buffer *b = buffers_find_by_filename(buffers, &p[7]);
    free((void *)p);

    if (b != NULL) {
      struct lsp_diagnostics *ld = lsp_server_diagnostics(server);
      struct lsp_buffer_diagnostics *diagnostics =
          diagnostics_for_buffer(ld, b);
      if (diagnostics == NULL) {
        VEC_APPEND(&ld->buffer_diagnostics,
                   struct lsp_buffer_diagnostics * new_diag);
        new_diag->buffer = b;
        new_diag->layer = buffer_add_text_property_layer(b);
        new_diag->diagnostics.nentries = 0;
        new_diag->diagnostics.capacity = 0;
        new_diag->diagnostics.temp = NULL;
        new_diag->diagnostics.entries = NULL;

        diagnostics = new_diag;
      }

      VEC_FOR_EACH(&diagnostics->diagnostics, struct diagnostic * diag) {
        diagnostic_free(diag);
      }
      VEC_DESTROY(&diagnostics->diagnostics);

      diagnostics->diagnostics = params.diagnostics;
      update_diagnostics_buffer(server, buffers, diagnostics->diagnostics, b);
    } else {
      VEC_FOR_EACH(&params.diagnostics, struct diagnostic * diag) {
        diagnostic_free(diag);
      }
      VEC_DESTROY(&params.diagnostics);
      message("failed to find buffer with URI: %.*s", params.uri.l,
              params.uri.s);
    }
  } else {
    message("warning: unsupported LSP URI: %.*s", params.uri.l, params.uri.s);
  }

  s8delete(params.uri);
}

static struct lsp_diagnostics *
lsp_diagnostics_from_server(struct lsp_server *server) {
  return lsp_server_diagnostics(server);
}

int32_t next_diagnostic_cmd(struct command_ctx ctx, int argc,
                            const char **argv) {
  (void)ctx;
  (void)argc;
  (void)argv;

  struct buffer_view *bv = window_buffer_view(windows_get_active());

  struct lsp_server *server = lsp_server_for_lang_id(bv->buffer->lang.id);
  if (!lsp_server_active(server)) {
    minibuffer_echo_timeout(2, "no working lsp for buffer %s",
                            bv->buffer->name);
    return 0;
  }

  struct lsp_buffer_diagnostics *diagnostics =
      diagnostics_for_buffer(lsp_diagnostics_from_server(server), bv->buffer);
  if (diagnostics == NULL) {
    return 0;
  }

  if (VEC_EMPTY(&diagnostics->diagnostics)) {
    minibuffer_echo_timeout(4, "no more diagnostics");
    return 0;
  }

  VEC_FOR_EACH(&diagnostics->diagnostics, struct diagnostic * diag) {
    if (location_compare(bv->dot, diag->region.begin) < 0) {
      buffer_view_goto(bv, diag->region.begin);
      return 0;
    }
  }

  buffer_view_goto(bv, VEC_FRONT(&diagnostics->diagnostics)->region.begin);
  return 0;
}

int32_t prev_diagnostic_cmd(struct command_ctx ctx, int argc,
                            const char **argv) {
  (void)ctx;
  (void)argc;
  (void)argv;

  struct buffer_view *bv = window_buffer_view(windows_get_active());

  struct lsp_server *server = lsp_server_for_lang_id(bv->buffer->lang.id);
  if (!lsp_server_active(server)) {
    minibuffer_echo_timeout(2, "no working lsp for buffer %s",
                            bv->buffer->name);
    return 0;
  }

  struct lsp_buffer_diagnostics *diagnostics =
      diagnostics_for_buffer(lsp_diagnostics_from_server(server), bv->buffer);

  if (diagnostics == NULL) {
    return 0;
  }

  if (VEC_EMPTY(&diagnostics->diagnostics)) {
    minibuffer_echo_timeout(4, "no more diagnostics");
    return 0;
  }

  VEC_FOR_EACH(&diagnostics->diagnostics, struct diagnostic * diag) {
    if (location_compare(bv->dot, diag->region.begin) > 0) {
      buffer_view_goto(bv, diag->region.begin);
      return 0;
    }
  }

  buffer_view_goto(bv, VEC_BACK(&diagnostics->diagnostics)->region.begin);
  return 0;
}

int32_t diagnostics_cmd(struct command_ctx ctx, int argc, const char **argv) {
  (void)argc;
  (void)argv;

  struct buffer *b = window_buffer(ctx.active_window);
  struct lsp_server *server = lsp_server_for_buffer(b);

  if (!lsp_server_active(server)) {
    minibuffer_echo_timeout(2, "buffer %s does not have lsp enabled", b->name);
    return 0;
  }

  struct lsp_buffer_diagnostics *d =
      diagnostics_for_buffer(lsp_diagnostics_from_server(server), b);
  struct buffer *db =
      update_diagnostics_buffer(server, ctx.buffers, d->diagnostics, b);
  window_set_buffer(ctx.active_window, db);

  return 0;
}

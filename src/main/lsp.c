#include "lsp.h"

#include "dged/buffer.h"
#include "dged/buffers.h"
#include "dged/hash.h"
#include "dged/hashmap.h"
#include "dged/lsp.h"
#include "dged/minibuffer.h"
#include "dged/reactor.h"
#include "dged/settings.h"

HASHMAP_ENTRY_TYPE(lsp_entry, struct lsp *);

HASHMAP(struct lsp_entry) g_lsp_clients;

static struct create_data {
  struct reactor *reactor;
  struct buffers *buffers;
} g_create_data;

static void log_message(int type, struct s8 msg) {
  (void)type;
  message("%s", msg);
}

static void create_lsp_client(struct buffer *buffer, void *userdata) {
  (void)userdata;

  struct create_data *data = &g_create_data;
  const char *id = buffer->lang.id;
  HASHMAP_GET(&g_lsp_clients, struct lsp_entry, id, struct lsp * *lsp);
  if (lsp == NULL) {
    // we need to start a new server
    struct setting *s = lang_setting(&buffer->lang, "language-server");
    if (!s) { // no language server set
      return;
    }

    char *const command[] = {s->value.data.string_value, NULL};

    char bufname[1024] = {0};
    snprintf(bufname, 1024, "*%s-lsp-stderr*", command[0]);
    struct buffer *stderr_buf = buffers_find(data->buffers, bufname);
    if (stderr_buf == NULL) {
      struct buffer buf = buffer_create(bufname);
      buf.lazy_row_add = false;
      stderr_buf = buffers_add(data->buffers, buf);
      buffer_set_readonly(stderr_buf, true);
    }

    struct lsp_client client_impl = {
        .log_message = log_message,
    };
    struct lsp *new_lsp =
        lsp_create(command, data->reactor, stderr_buf, client_impl, NULL);

    if (new_lsp == NULL) {
      minibuffer_echo("failed to create language server %s", command[0]);
      buffers_remove(data->buffers, bufname);
      return;
    }

    HASHMAP_APPEND(&g_lsp_clients, struct lsp_entry, id,
                   struct lsp_entry * new);
    new->value = new_lsp;

    if (lsp_start_server(new_lsp) < 0) {
      minibuffer_echo("failed to start language server %s process.",
                      lsp_server_name(new_lsp));
      return;
    }
  }
}

static void set_default_lsp(const char *lang_id, const char *server) {
  struct language l = lang_from_id(lang_id);
  if (!lang_is_fundamental(&l)) {
    lang_setting_set_default(
        &l, "language-server",
        (struct setting_value){.type = Setting_String,
                               .data.string_value = (char *)server});
    lang_destroy(&l);
  }
}

void lang_servers_init(struct reactor *reactor, struct buffers *buffers) {
  HASHMAP_INIT(&g_lsp_clients, 32, hash_name);

  set_default_lsp("c", "clangd");
  set_default_lsp("rs", "rust-analyzer");
  set_default_lsp("python", "pylsp");

  g_create_data.reactor = reactor;
  g_create_data.buffers = buffers;
  buffer_add_create_hook(create_lsp_client, NULL);
}

void lang_servers_update(void) {
  HASHMAP_FOR_EACH(&g_lsp_clients, struct lsp_entry * e) {
    lsp_update(e->value, NULL, 0);
  }
}

void lang_servers_teardown(void) {
  HASHMAP_FOR_EACH(&g_lsp_clients, struct lsp_entry * e) {
    lsp_stop_server(e->value);
  }
}

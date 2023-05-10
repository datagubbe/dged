#include "dged/binding.h"
#include "dged/buffer.h"
#include "dged/minibuffer.h"
#include "dged/vec.h"

static struct keymap g_global_keymap, g_ctrlx_map, g_windows_keymap,
    g_buffer_default_keymap;

struct buffer_keymap {
  struct buffer *buffer;
  bool active;
  struct keymap keymap;
};

static VEC(struct buffer_keymap) g_buffer_keymaps;

void set_default_buffer_bindings(struct keymap *keymap) {
  struct binding buffer_bindings[] = {
      BINDING(Ctrl, 'B', "backward-char"),
      BINDING(LEFT, "backward-char"),
      BINDING(Ctrl, 'F', "forward-char"),
      BINDING(RIGHT, "forward-char"),

      BINDING(Ctrl, 'P', "backward-line"),
      BINDING(UP, "backward-line"),
      BINDING(Ctrl, 'N', "forward-line"),
      BINDING(DOWN, "forward-line"),

      BINDING(Meta, 'f', "forward-word"),
      BINDING(Meta, 'b', "backward-word"),

      BINDING(Ctrl, 'A', "beginning-of-line"),
      BINDING(Ctrl, 'E', "end-of-line"),

      BINDING(Ctrl, 'S', "find-next"),
      BINDING(Ctrl, 'R', "find-prev"),

      BINDING(Meta, 'g', "goto-line"),
      BINDING(Meta, '<', "goto-beginning"),
      BINDING(Meta, '>', "goto-end"),

      BINDING(Ctrl, 'V', "scroll-down"),
      BINDING(Meta, 'v', "scroll-up"),
      BINDING(Spec, '6', "scroll-down"),
      BINDING(Spec, '5', "scroll-up"),

      BINDING(ENTER, "newline"),
      BINDING(TAB, "indent"),

      BINDING(Ctrl, 'K', "kill-line"),
      BINDING(DELETE, "delete-char"),
      BINDING(Ctrl, 'D', "delete-char"),
      BINDING(Meta, 'd', "delete-word"),
      BINDING(Meta, 'D', "backward-delete-word"),
      BINDING(BACKSPACE, "backward-delete-char"),

      BINDING(Ctrl, '@', "set-mark"),

      BINDING(Ctrl, 'W', "cut"),
      BINDING(Ctrl, 'Y', "paste"),
      BINDING(Meta, 'y', "paste-older"),
      BINDING(Meta, 'w', "copy"),

      BINDING(Ctrl, '_', "undo"),
  };

  keymap_bind_keys(keymap, buffer_bindings,
                   sizeof(buffer_bindings) / sizeof(buffer_bindings[0]));
}

struct keymap *register_bindings() {
  g_global_keymap = keymap_create("global", 32);
  g_ctrlx_map = keymap_create("c-x", 32);
  g_windows_keymap = keymap_create("c-x w", 32);

  struct binding global_binds[] = {
      PREFIX(Ctrl, 'X', &g_ctrlx_map),
      BINDING(Ctrl, 'G', "abort"),
      BINDING(Meta, 'x', "run-command-interactive"),
  };

  struct binding ctrlx_bindings[] = {
      BINDING(Ctrl, 'C', "exit"),
      BINDING(Ctrl, 'S', "buffer-write-to-file"),
      BINDING(Ctrl, 'F', "find-file"),
      BINDING(Ctrl, 'W', "write-file"),
      BINDING(None, 'b', "switch-buffer"),
      BINDING(Ctrl, 'B', "buffer-list"),

      BINDING(None, '0', "window-close"),
      BINDING(None, '1', "window-close-others"),
      BINDING(None, '2', "window-split-horizontal"),
      BINDING(None, '3', "window-split-vertical"),
      BINDING(None, 'o', "window-focus-next"),

      PREFIX(None, 'w', &g_windows_keymap),
  };

  // windows
  struct binding window_subbinds[] = {
      BINDING(None, '0', "window-focus-0"),
      BINDING(None, '1', "window-focus-1"),
      BINDING(None, '2', "window-focus-2"),
      BINDING(None, '3', "window-focus-3"),
      BINDING(None, '4', "window-focus-4"),
      BINDING(None, '5', "window-focus-5"),
      BINDING(None, '6', "window-focus-6"),
      BINDING(None, '7', "window-focus-7"),
      BINDING(None, '8', "window-focus-8"),
      BINDING(None, '9', "window-focus-9"),
  };

  // buffers
  g_buffer_default_keymap = keymap_create("buffer-default", 128);
  set_default_buffer_bindings(&g_buffer_default_keymap);

  keymap_bind_keys(&g_windows_keymap, window_subbinds,
                   sizeof(window_subbinds) / sizeof(window_subbinds[0]));
  keymap_bind_keys(&g_global_keymap, global_binds,
                   sizeof(global_binds) / sizeof(global_binds[0]));
  keymap_bind_keys(&g_ctrlx_map, ctrlx_bindings,
                   sizeof(ctrlx_bindings) / sizeof(ctrlx_bindings[0]));

  VEC_INIT(&g_buffer_keymaps, 32);

  return &g_global_keymap;
}

struct keymap *buffer_default_bindings() {
  return &g_buffer_default_keymap;
}

int32_t execute(struct command_ctx ctx, int argc, const char *argv[]) {
  // TODO: this should be more lib-like
  return minibuffer_execute();
}

static struct command execute_minibuffer_command = {
    .fn = execute,
    .name = "minibuffer-execute",
    .userdata = NULL,
};

void buffer_bind_keys(struct buffer *buffer, struct binding *bindings,
                      uint32_t nbindings) {
  struct buffer_keymap *target = NULL;
  VEC_FOR_EACH(&g_buffer_keymaps, struct buffer_keymap * km) {
    if (buffer == km->buffer) {
      target = km;
    }
  }

  if (target == NULL) {
    struct buffer_keymap new = (struct buffer_keymap){
        .buffer = buffer,
        .active = false,
    };
    VEC_PUSH(&g_buffer_keymaps, new);
    target = VEC_BACK(&g_buffer_keymaps);
  }

  if (!target->active) {
    target->keymap = keymap_create("buffer-overlay-keys", 32);
    target->active = true;
    set_default_buffer_bindings(&target->keymap);
  }

  keymap_bind_keys(&target->keymap, bindings, nbindings);
}

// TODO: do something better
void reset_buffer_keys(struct buffer *buffer) {
  VEC_FOR_EACH(&g_buffer_keymaps, struct buffer_keymap * km) {
    if (buffer == km->buffer) {
      keymap_destroy(&km->keymap);
      km->active = false;
    }
  }
}

struct keymap *buffer_keymap(struct buffer *buffer) {
  VEC_FOR_EACH(&g_buffer_keymaps, struct buffer_keymap * km) {
    if (buffer == km->buffer && km->active) {
      return &km->keymap;
    }
  }

  return &g_buffer_default_keymap;
}

void reset_minibuffer_keys(struct buffer *minibuffer) {
  reset_buffer_keys(minibuffer);
  struct binding bindings[] = {
      ANONYMOUS_BINDING(ENTER, &execute_minibuffer_command),
  };

  buffer_bind_keys(minibuffer, bindings,
                   sizeof(bindings) / sizeof(bindings[0]));
}

void destroy_keymaps() {
  keymap_destroy(&g_windows_keymap);
  keymap_destroy(&g_global_keymap);
  keymap_destroy(&g_ctrlx_map);
  keymap_destroy(&g_buffer_default_keymap);

  VEC_FOR_EACH(&g_buffer_keymaps, struct buffer_keymap * km) {
    if (km->active) {
      keymap_destroy(&km->keymap);
      km->active = false;
    }
  }

  VEC_DESTROY(&g_buffer_keymaps);
}

#define _DEFAULT_SOURCE
#include "dired.h"

#include "dged/allocator.h"
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

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum direntry_type {
  Direntry_Directory,
  Direntry_File,
  Direntry_Link,
  Direntry_Other,
};

/* platform-independent directory entry */
struct direntry {
  struct s8 name;
  struct s8 target;
  enum direntry_type type;
  struct s8 user;
  struct s8 group;
  struct s8 permissions;
  size_t size;
  uint64_t mtime_ns;
};

typedef VEC(struct direntry) direntry_vec;

struct dired_state {
  struct s8 current_path;
  direntry_vec entries;
};

struct read_directory_res {
  bool success;
  direntry_vec entries;
};

#if defined(_WIN32)
#error "Implement me!"
#else

static struct s8 mode_string(mode_t mode) {
  struct s8 modestr = s8new("----------", 10);
  modestr.s[0] = (S_ISDIR(mode)) ? 'd' : '-';  // directory or not
  modestr.s[1] = (mode & S_IRUSR) ? 'r' : '-'; // owner read
  modestr.s[2] = (mode & S_IWUSR) ? 'w' : '-'; // owner write
  if (mode & S_IXUSR) {
    modestr.s[3] = (mode & S_ISUID) ? 's' : 'x'; // owner execute
  } else {
    modestr.s[3] = (mode & S_ISUID) ? 'S' : '-'; // setuid
  }

  modestr.s[4] = (mode & S_IRGRP) ? 'r' : '-'; // group read
  modestr.s[5] = (mode & S_IWGRP) ? 'w' : '-'; // group write
  if (mode & S_IXGRP) {
    modestr.s[6] = (mode & S_ISGID) ? 's' : 'x'; // group execute
  } else {
    modestr.s[6] = (mode & S_ISGID) ? 'S' : '-'; // setgid
  }

  modestr.s[7] = (mode & S_IROTH) ? 'r' : '-'; // other read
  modestr.s[8] = (mode & S_IWOTH) ? 'w' : '-'; // other write
  modestr.s[9] = (mode & S_IXOTH) ? 'x' : '-'; // other execute

  return modestr;
}

static struct read_directory_res read_directory(const char *path,
                                                bool include_hidden) {
  DIR *d = opendir(path);
  if (d == NULL) {
    minibuffer_echo_timeout(4, "dired: failed to open directory %s: %s", path,
                            strerror(errno));

    return (struct read_directory_res){
        .success = false,
    };
  }

  int dir_fd = dirfd(d);
  if (dir_fd == -1) {
    minibuffer_echo_timeout(4, "dired: failed to run dirfd on %s: %s", path,
                            strerror(errno));

    return (struct read_directory_res){
        .success = false,
    };
  }

  direntry_vec entries;
  VEC_INIT(&entries, 16);

  errno = 0;
  struct dirent *de = readdir(d);
  while (de != NULL && errno == 0) {

    if (!include_hidden) {
      struct s8 n = s8(de->d_name);
      if (s8startswith(n, s8(".")) && !s8eq(n, s8(".")) && !s8eq(n, s8(".."))) {
        de = readdir(d);
        continue;
      }
    }

    VEC_APPEND(&entries, struct direntry * newent);
    newent->name = s8new(de->d_name, strlen(de->d_name));
    switch (de->d_type) {
    case DT_DIR:
      newent->type = Direntry_Directory;
      break;

    case DT_LNK:
      newent->type = Direntry_Link;
      break;

    case DT_REG:
      newent->type = Direntry_File;
      break;

    default:
      newent->type = Direntry_Other;
    }

    struct stat sb;
    int res = fstatat(dir_fd, de->d_name, &sb, AT_SYMLINK_NOFOLLOW);
    if (res == 0) {
      newent->mtime_ns = sb.st_mtim.tv_sec * 1e9 + sb.st_mtim.tv_nsec;

      struct passwd *user = getpwuid(sb.st_uid);
      if (user != NULL) {
        newent->user = s8new(user->pw_name, strlen(user->pw_name));
      }

      struct group *grp = getgrgid(sb.st_gid);
      if (grp != NULL) {
        newent->group = s8new(grp->gr_name, strlen(grp->gr_name));
      }

      newent->size = sb.st_size;
      newent->permissions = mode_string(sb.st_mode);

      newent->target = (struct s8){.s = NULL, .l = 0};
      if (newent->type == Direntry_Link) {
        char buf[1024];
        ssize_t res = readlinkat(dir_fd, de->d_name, buf, 1024);
        if (res >= 0) {
          newent->target = s8new(buf, res);
        }
      }
    }

    de = readdir(d);
  }
  closedir(d);

  return (struct read_directory_res){
      .entries = entries,
      .success = true,
  };
}
#endif

static void set_fg_color(struct buffer *buffer, struct region range,
                         int color) {
  buffer_add_text_property(buffer, range.begin, range.end,
                           (struct text_property){
                               .type = TextProperty_Colors,
                               .data.colors =
                                   (struct text_property_colors){
                                       .set_fg = true,
                                       .fg = color,
                                   },
                           });
}

static struct location direntry_render_name(struct buffer *buffer,
                                            struct location at,
                                            struct direntry *entry) {
  struct location namebegin = at;
  at = buffer_add(buffer, at, entry->name.s, entry->name.l);

  int color = Color_Magenta;
  bool set_color = false;
  switch (entry->type) {
  case Direntry_Directory:
    color = Color_Magenta;
    set_color = true;
    break;
  case Direntry_Link:
    color = Color_Green;
    set_color = true;
    break;
  case Direntry_File:
    break;
  default:
    color = Color_Cyan;
    set_color = true;
  }

  if (set_color) {
    set_fg_color(buffer, region_new(namebegin, at), color);
  }

  if (entry->type == Direntry_Link && !s8empty(entry->target)) {
    at = buffer_add(buffer, at, (uint8_t *)" -> ", 4);
    struct location linktarget_begin = at;
    at = buffer_add(buffer, at, entry->target.s, entry->target.l);
    set_fg_color(buffer, region_new(linktarget_begin, at), Color_Cyan);
  }

  return at;
}

struct human_file_size {
  double size;
  struct s8 unit;
};

static struct human_file_size to_human(size_t bytes) {

  const struct s8 suffixes[] = {s8(""),    s8("KiB"), s8("MiB"), s8("GiB"),
                                s8("TiB"), s8("PiB"), s8("EiB"), s8("ZiB"),
                                s8("YiB"), s8("RiB"), s8("QiB")};

  size_t unit_index = 0;
  double bts = bytes;

  while (bts > 1024.f) {
    bts /= 1024.f;
    ++unit_index;
  }

  return (struct human_file_size){.size = bts,
                                  .unit = unit_index < 10 ? suffixes[unit_index]
                                                          : suffixes[10]};
}

static struct location direntry_render(struct buffer *buffer,
                                       struct location at,
                                       struct direntry *entry) {

  // permissions/mode
  at = buffer_add(buffer, at, entry->permissions.s, entry->permissions.l);
  at = buffer_add(buffer, at, (uint8_t *)" ", 1);

  // user
  at = buffer_add(buffer, at, entry->user.s, entry->user.l);
  at = buffer_add(buffer, at, (uint8_t *)" ", 1);

  // group
  at = buffer_add(buffer, at, entry->group.s, entry->group.l);
  at = buffer_add(buffer, at, (uint8_t *)" ", 1);

  // size
  struct human_file_size fs = to_human(entry->size);
  if (!s8empty(fs.unit)) {
    struct s8 size_str = s8from_fmt("%4.3g %3s ", fs.size, fs.unit);
    at = buffer_add(buffer, at, size_str.s, size_str.l);
    s8delete(size_str);
  } else {
    struct s8 size_str = s8from_fmt("%8ld ", entry->size);
    at = buffer_add(buffer, at, size_str.s, size_str.l);
    s8delete(size_str);
  }

  // mtime
  time_t t = entry->mtime_ns / 1e9;
  struct tm *tm = localtime(&t);
  char buf[1024];
  size_t res = strftime(buf, 1024, "%F %R ", tm);
  if (res > 0) {
    at = buffer_add(buffer, at, (uint8_t *)buf, res);
  }

  // name
  at = direntry_render_name(buffer, at, entry);
  at = buffer_newline(buffer, at);
  return at;
}

static int direntry_comp(const void *d1, const void *d2) {
  struct direntry *de1 = (struct direntry *)d1;
  struct direntry *de2 = (struct direntry *)d2;

  if (de1->type == Direntry_Directory && de2->type != Direntry_Directory) {
    return -1;
  } else if (de2->type == Direntry_Directory &&
             de1->type != Direntry_Directory) {
    return 1;
  }

  return s8icmp(de1->name, de2->name);
}

static void direntry_delete(struct direntry *entry) {
  s8delete(entry->name);
  s8delete(entry->target);
  s8delete(entry->group);
  s8delete(entry->user);
  s8delete(entry->permissions);
}

static struct read_directory_res dired_refresh(struct buffer *buffer,
                                               struct s8 path) {
  buffer_set_readonly(buffer, false);
  buffer_clear(buffer);
  buffer_begin_bulk_add(buffer);
  struct location at = {}, begin = {};

  const char *p = s8tocstr(path);
  struct read_directory_res res = read_directory(p, false);
  free((void *)p);
  if (!res.success) {
    return res;
  }

  qsort(res.entries.entries, VEC_SIZE(&res.entries), sizeof(struct direntry),
        direntry_comp);

  VEC_FOR_EACH(&res.entries, struct direntry * ent) {
    at = direntry_render(buffer, at, ent);
  }

  buffer_end_bulk_add(buffer, region_new(begin, at));
  buffer_set_readonly(buffer, true);
  return res;
}

static void dired_buf_destroyed(struct buffer *buffer, void *userdata) {
  (void)buffer;

  struct dired_state *state = (struct dired_state *)userdata;

  s8delete(state->current_path);
  state->current_path.s = NULL;
  state->current_path.l = 0;
  VEC_FOR_EACH(&state->entries, struct direntry * ent) { direntry_delete(ent); }
  VEC_DESTROY(&state->entries);
}

static struct direntry *get_direntry(direntry_vec entries, size_t line) {
  if (line >= VEC_SIZE(&entries)) {
    return NULL;
  }

  return &VEC_ENTRIES(&entries)[line];
}

static int32_t dired_visit_cmd(struct command_ctx ctx, int argc,
                               const char *argv[]) {
  (void)argc;
  (void)argv;

  struct dired_state *state = (struct dired_state *)ctx.userdata;

  struct buffer_view *view = window_buffer_view(windows_get_active());
  struct direntry *entry = get_direntry(state->entries, view->dot.line);

  if (entry != NULL) {
    struct command *cmd = lookup_command(ctx.commands, "find-file");
    if (cmd != NULL) {
      struct s8 new_path = join_path(state->current_path, entry->name);
      struct s8 path = canonicalize(new_path);
      const char *args[] = {s8ascstr(path)};
      int32_t res = execute_command(cmd, ctx.commands, ctx.active_window,
                                    ctx.buffers, ctx.display, 1, args);

      s8delete(new_path);
      s8delete(path);

      return res;
    }
  }

  return 0;
}

static void dired_do_refresh(struct buffers *buffers,
                             struct dired_state *state) {
  struct buffer *dired_buf = buffers_find(buffers, "*dired*");
  struct read_directory_res res = dired_refresh(dired_buf, state->current_path);

  VEC_FOR_EACH(&state->entries, struct direntry * ent) { direntry_delete(ent); }
  VEC_DESTROY(&state->entries);

  if (res.success) {
    state->entries = res.entries;
  }
}

static int32_t dired_refresh_cmd(struct command_ctx ctx, int argc,
                                 const char *argv[]) {
  (void)argv;
  (void)argc;

  struct dired_state *state = (struct dired_state *)ctx.userdata;
  dired_do_refresh(ctx.buffers, state);

  return 0;
}

static int32_t dired_mkdir_cmd(struct command_ctx ctx, int argc,
                               const char *argv[]) {

  if (argc < 1) {
    return minibuffer_prompt(ctx, "create directory: ");
  }

  struct dired_state *state = (struct dired_state *)ctx.userdata;
  struct s8 path_to_create = s8(argv[0]);

  if (!is_relative(path_to_create)) {
    minibuffer_echo_timeout(4, "Path to create is not relative");
    return 1;
  }

  struct s8 fullpath = join_path(state->current_path, path_to_create);
  if (!create_directories(fullpath)) {
    minibuffer_echo_timeout(
        4, "Failed to create directory \"%s\" at \"%s\": \"%s\"",
        s8ascstr(path_to_create), s8ascstr(state->current_path),
        strerror(errno));
    s8delete(fullpath);
    return 1;
  }

  s8delete(fullpath);
  dired_do_refresh(ctx.buffers, state);

  return 0;
}

struct dired_rm_state {
  struct dired_state *state;
  buffer_keymap_id keymap_id;
};

static int32_t dired_do_rm(struct command_ctx ctx, int argc,
                           const char *argv[]) {
  (void)argc;
  (void)argv;

  struct dired_rm_state *rm_state = (struct dired_rm_state *)ctx.userdata;
  buffer_remove_keymap(rm_state->keymap_id);
  minibuffer_abort_prompt();

  struct buffer *dired_buf = buffers_find(ctx.buffers, "*dired*");
  if (dired_buf == NULL) {
    minibuffer_echo_timeout(4, "dired buffer does not exist");
    return 1;
  }

  struct dired_state *state = rm_state->state;
  struct buffer_view *view = window_buffer_view(windows_get_active());

  if (view->buffer != dired_buf) {
    minibuffer_echo_timeout(4, "dired buffer is not active");
    return 1;
  }

  size_t idx = view->dot.line;
  if (idx >= VEC_SIZE(&state->entries)) {
    minibuffer_echo_timeout(4, "dired rm index is out of range");
    return 1;
  }

  struct direntry *entry = &VEC_ENTRIES(&state->entries)[idx];

  struct s8 path_to_remove = entry->name;
  struct s8 fullpath = join_path(state->current_path, path_to_remove);
  remove_recursive(fullpath);

  s8delete(fullpath);
  dired_do_refresh(ctx.buffers, state);

  free(rm_state);

  return 0;
}

static int32_t dired_cancel_rm(struct command_ctx ctx, int argc,
                               const char *argv[]) {
  (void)argc;
  (void)argv;

  struct dired_rm_state *rm_state = (struct dired_rm_state *)ctx.userdata;
  buffer_remove_keymap(rm_state->keymap_id);
  minibuffer_abort_prompt();
  minibuffer_echo_timeout(4, "delete aborted");

  free(rm_state);
  return 0;
}

static int32_t dired_rm_cmd(struct command_ctx ctx, int argc,
                            const char *argv[]) {

  (void)argc;
  (void)argv;

  struct dired_rm_state *rm_state = calloc(1, sizeof(struct dired_rm_state));
  rm_state->state = (struct dired_state *)ctx.userdata;

  static struct command dired_do_rm_ = {
      .name = "dired-do-rm",
      .fn = dired_do_rm,
  };

  static struct command dired_cancel_rm_ = {
      .name = "dired-cancel-rm",
      .fn = dired_cancel_rm,
  };

  dired_do_rm_.userdata = rm_state;
  dired_cancel_rm_.userdata = rm_state;
  struct binding bindings[] = {
      ANONYMOUS_BINDING(None, 'y', &dired_do_rm_),
      ANONYMOUS_BINDING(None, 'n', &dired_cancel_rm_),
  };
  struct keymap m = keymap_create("dired-accept-rm", 2);
  keymap_bind_keys(&m, bindings, sizeof(bindings) / sizeof(bindings[0]));
  rm_state->keymap_id = buffer_add_keymap(minibuffer_buffer(), m);
  return minibuffer_prompt(ctx, "recursively delete? [y/n] ");
}

static int32_t dired_cmd(struct command_ctx ctx, int argc, const char *argv[]) {
  const char *path = ".";
  if (argc > 0) {
    path = argv[0];
  }

  struct dired_state *state = (struct dired_state *)ctx.userdata;

  s8delete(state->current_path);
  state->current_path = canonicalize(s8(path));

  struct buffer *dired_buf = buffers_find(ctx.buffers, "*dired*");
  if (dired_buf == NULL) {
    struct buffer new_buf = buffer_create("*dired*");
    new_buf.lazy_row_add = false;
    new_buf.retain_properties = true;
    dired_buf = buffers_add(ctx.buffers, new_buf);

    buffer_add_destroy_hook(dired_buf, dired_buf_destroyed, state);

    static struct command dired_visit = {
        .name = "dired-visit",
        .fn = dired_visit_cmd,
    };

    static struct command dired_refresh = {
        .name = "dired-refresh",
        .fn = dired_refresh_cmd,
    };

    static struct command dired_mkdir = {
        .name = "dired-mkdir",
        .fn = dired_mkdir_cmd,
    };

    static struct command dired_rm = {
        .name = "dired-rm",
        .fn = dired_rm_cmd,
    };

    dired_visit.userdata = state;
    dired_refresh.userdata = state;
    dired_mkdir.userdata = state;
    dired_rm.userdata = state;
    struct binding bindings[] = {
        ANONYMOUS_BINDING(ENTER, &dired_visit),
        ANONYMOUS_BINDING(NUMPAD_ENTER, &dired_visit),
        ANONYMOUS_BINDING(Ctrl, 'K', &dired_rm),
        ANONYMOUS_BINDING(DELETE, &dired_rm),
        ANONYMOUS_BINDING(None, 'g', &dired_refresh),
        ANONYMOUS_BINDING(None, '+', &dired_mkdir),
    };

    struct keymap km = keymap_create("dired", 16);
    keymap_bind_keys(&km, bindings, sizeof(bindings) / sizeof(bindings[0]));
    buffer_add_keymap(dired_buf, km);
  }

  free(dired_buf->associated_path);
  dired_buf->associated_path = s8tocstr(state->current_path);

  VEC_FOR_EACH(&state->entries, struct direntry * ent) { direntry_delete(ent); }
  VEC_DESTROY(&state->entries);

  struct read_directory_res res = dired_refresh(dired_buf, state->current_path);
  if (res.success) {
    state->entries = res.entries;
    window_set_buffer(windows_get_active(), dired_buf);
  }

  return 0;
}

void register_dired_commands(struct commands *commands,
                             struct frame_allocator *alloc) {
  struct dired_state *state =
      frame_allocator_alloc(alloc, sizeof(struct dired_state));
  memset(state, 0, sizeof(struct dired_state));

  register_command(commands, (struct command){
                                 .name = "dired",
                                 .fn = dired_cmd,
                                 .userdata = state,
                             });
}

bool dired_is_dired_buffer(struct buffer *buffer) {
  return strcmp(buffer->name, "*dired*") == 0;
}

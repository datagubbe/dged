#include "settings.h"
#include "command.h"
#include "hash.h"
#include "hashmap.h"
#include "minibuffer.h"
#include "settings-parse.h"
#include "utf8.h"
#include "vec.h"

#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct settings g_settings = {0};

void settings_init(uint32_t initial_capacity) {
  HASHMAP_INIT(&g_settings.settings, initial_capacity, hash_name);
}

void settings_destroy() {
  HASHMAP_FOR_EACH(&g_settings.settings, struct setting_entry * entry) {
    struct setting *setting = &entry->value;
    if (setting->value.type == Setting_String) {
      free(setting->value.string_value);
    }
  }

  HASHMAP_DESTROY(&g_settings.settings);
}

void setting_set_value(struct setting *setting, struct setting_value val) {
  if (setting->value.type == val.type) {
    if (setting->value.type == Setting_String && val.string_value != NULL) {
      setting->value.string_value = strdup(val.string_value);
    } else {
      setting->value = val;
    }
  }
}

static void settings_register_setting(const char *path,
                                      struct setting_value default_value) {
  HASHMAP_APPEND(&g_settings.settings, struct setting_entry, path,
                 struct setting_entry * s);

  if (s != NULL) {
    struct setting *new_setting = &s->value;
    new_setting->value.type = default_value.type;
    setting_set_value(new_setting, default_value);
    strncpy(new_setting->path, path, 128);
    new_setting->path[127] = '\0';
  }
}

struct setting *settings_get(const char *path) {
  HASHMAP_GET(&g_settings.settings, struct setting_entry, path,
              struct setting * s);
  return s;
}

void settings_get_prefix(const char *prefix, struct setting **settings_out[],
                         uint32_t *nsettings_out) {

  VEC(struct setting *) res;
  VEC_INIT(&res, 16);
  HASHMAP_FOR_EACH(&g_settings.settings, struct setting_entry * entry) {
    struct setting *setting = &entry->value;
    if (strncmp(prefix, setting->path, strlen(prefix)) == 0) {
      VEC_PUSH(&res, setting);
    }
  }

  *nsettings_out = VEC_SIZE(&res);
  *settings_out = VEC_ENTRIES(&res);

  VEC_DISOWN_ENTRIES(&res);
  VEC_DESTROY(&res);
}

void settings_set(const char *path, struct setting_value value) {
  struct setting *setting = settings_get(path);
  if (setting != NULL) {
    setting_set_value(setting, value);
  } else {
    settings_register_setting(path, value);
  }
}

void settings_set_default(const char *path, struct setting_value value) {
  struct setting *setting = settings_get(path);
  if (setting == NULL) {
    settings_register_setting(path, value);
  }
}

void setting_to_string(struct setting *setting, char *buf, size_t n) {
  switch (setting->value.type) {
  case Setting_Bool:
    snprintf(buf, n, "%s", setting->value.bool_value ? "true" : "false");
    break;
  case Setting_Number:
    snprintf(buf, n, "%" PRId64, setting->value.number_value);
    break;
  case Setting_String:
    snprintf(buf, n, "%s", setting->value.string_value);
    break;
  }
}

static int32_t parse_toml(struct parser *state, char **errmsgs[]) {
  char *curtbl = NULL;
  char *curkey = NULL;
  uint32_t errcnt = 0;

  VEC(char *) errs;
  VEC_INIT(&errs, 16);

  struct token t = {0};
  int64_t i = 0;
  bool b = false;
  char *v = NULL, *err = NULL;
  while (parser_next_token(state, &t)) {
    switch (t.type) {
    case Token_Table:
      if (curtbl != NULL) {
        free(curtbl);
      }
      curtbl = calloc(t.len + 1, 1);
      strncpy(curtbl, (char *)t.data, t.len);
      break;

    case Token_InlineTable:
      if (curkey != NULL) {
        free(curtbl);
        curtbl = strdup(curkey);
      }
      break;

    case Token_Key:
      if (curkey != NULL) {
        free(curkey);
      }
      uint32_t len = t.len + 1;
      if (curtbl != NULL) {
        len += strlen(curtbl) /* space for the . */ + 1;
      }

      curkey = calloc(len, 1);
      if (curtbl != NULL) {
        strcpy(curkey, curtbl);
        curkey[strlen(curtbl)] = '.';
      }

      strncat(curkey, (char *)t.data, t.len);
      break;

    case Token_IntValue:
      i = *((int64_t *)t.data);
      settings_set(curkey, (struct setting_value){.type = Setting_Number,
                                                  .number_value = i});
      break;

    case Token_BoolValue:
      b = *((bool *)t.data);
      settings_set(curkey, (struct setting_value){.type = Setting_Bool,
                                                  .bool_value = b});
      break;

    case Token_StringValue:
      v = calloc(t.len + 1, 1);
      strncpy(v, (char *)t.data, t.len);
      settings_set(curkey, (struct setting_value){.type = Setting_String,
                                                  .string_value = v});
      free(v);
      break;

    case Token_Error:
      err = malloc(t.len + 128);
      snprintf(err, t.len + 128, "error (%d:%d): %.*s\n", t.row, t.col, t.len,
               (char *)t.data);
      VEC_PUSH(&errs, err);
      break;

    case Token_Comment:
      break;
    }
  }

  if (curtbl != NULL) {
    free(curtbl);
  }

  if (curkey != NULL) {
    free(curkey);
  }

  uint32_t ret = 0;
  if (!VEC_EMPTY(&errs)) {
    *errmsgs = VEC_ENTRIES(&errs);
    ret = VEC_SIZE(&errs);

    VEC_DISOWN_ENTRIES(&errs);
    VEC_DESTROY(&errs);
  } else {
    *errmsgs = NULL;
    VEC_DESTROY(&errs);
  }
  return ret;
}

struct str_cursor {
  const char *data;
  uint32_t pos;
  uint32_t size;
};

size_t get_bytes_from_str(size_t nbytes, uint8_t *buf, void *userdata) {
  struct str_cursor *c = (struct str_cursor *)userdata;
  size_t left = c->size - c->pos;
  size_t to_copy = nbytes > left ? left : nbytes;
  if (to_copy > 0) {
    memcpy(buf, c->data + c->pos, to_copy);
  }

  c->pos += to_copy;

  return to_copy;
}

int32_t settings_from_string(const char *toml, char **errmsgs[]) {
  struct str_cursor cursor = {
      .data = toml,
      .pos = 0,
      .size = strlen(toml),
  };

  struct reader reader = {
      .getbytes = get_bytes_from_str,
      .userdata = (void *)&cursor,
  };

  struct parser parser = parser_create(reader);
  int32_t ret = parse_toml(&parser, errmsgs);

  parser_destroy(&parser);
  return ret;
}

#define FILE_READER_BUFSZ 1024
struct file_reader {
  int fd;
  uint8_t buffer[FILE_READER_BUFSZ];
  uint32_t buflen;
};

static struct file_reader file_reader_create(int fd) {
  return (struct file_reader){
      .fd = fd,
      .buffer = {0},
      .buflen = 0,
  };
}

static size_t get_bytes_from_file(size_t nbytes, uint8_t *buf, void *userdata) {
  struct file_reader *r = (struct file_reader *)userdata;
  if (nbytes > FILE_READER_BUFSZ) {
    return read(r->fd, buf, nbytes);
  }

  if (nbytes > r->buflen) {
    // fill buffer
    r->buflen +=
        read(r->fd, r->buffer + r->buflen, FILE_READER_BUFSZ - r->buflen);
  }

  size_t to_read = nbytes > r->buflen ? r->buflen : nbytes;
  memcpy(buf, r->buffer, to_read);

  r->buflen -= to_read;
  memmove(r->buffer, r->buffer + to_read, r->buflen);
  return to_read;
}

int32_t settings_from_file(const char *path, char **errmsgs[]) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    return fd;
  }

  struct file_reader file_reader = file_reader_create(fd);

  struct reader reader = {
      .getbytes = get_bytes_from_file,
      .userdata = (void *)&file_reader,
  };

  struct parser parser = parser_create(reader);
  int32_t ret = parse_toml(&parser, errmsgs);

  parser_destroy(&parser);
  return ret;
}

const char *setting_join_key(const char *initial, const char *setting) {
  size_t l1 = strlen(initial);
  size_t l2 = strlen(setting);
  char *combined = (char *)malloc(sizeof(char) * (l1 + l2 + 2));

  uint32_t idx = 0;
  memcpy(&combined[idx], initial, l1);
  idx += l1;
  combined[idx++] = '.';
  memcpy(&combined[idx], setting, l2);
  idx += l2;
  combined[idx++] = '\0';

  return combined;
}

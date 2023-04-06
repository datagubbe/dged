#ifndef _BTREE_H
#define _BTREE_H

#include "vec.h"

#include <stdlib.h>

#define BINTREE_ENTRY_TYPE(name, entry)                                        \
  struct name {                                                                \
    struct name *parent;                                                       \
    struct name *left;                                                         \
    struct name *right;                                                        \
    entry value;                                                               \
  }

#define BINTREE(entry)                                                         \
  struct {                                                                     \
    struct entry *root;                                                        \
  }

#define BINTREE_INIT(tree) ((tree)->root = NULL)
#define BINTREE_DESTROY(tree, entry_type) BINTREE_INIT(tree)

#define BINTREE_ROOT(tree) (tree)->root

#define BINTREE_LEFT(node) (node)->left
#define BINTREE_RIGHT(node) (node)->right
#define BINTREE_PARENT(node) (node)->parent
#define BINTREE_VALUE(node) (node)->value
#define BINTREE_HAS_PARENT(node) ((node)->parent != NULL)
#define BINTREE_HAS_LEFT(node) ((node)->left != NULL)
#define BINTREE_HAS_RIGHT(node) ((node)->right != NULL)

#define BINTREE_FREE_NODE(node) free(node)
#define BINTREE_FREE_NODES(root, entry_type)                                   \
  {                                                                            \
    BINTREE_FIRST(root);                                                       \
    VEC(struct entry_type *) to_delete;                                        \
    VEC_INIT(&to_delete, 10);                                                  \
    while (root != NULL) {                                                     \
      VEC_PUSH(&to_delete, root);                                              \
      BINTREE_NEXT(root);                                                      \
    }                                                                          \
    VEC_FOR_EACH(&to_delete, struct entry_type **e) { BINTREE_FREE_NODE(*e); } \
    VEC_DESTROY(&to_delete);                                                   \
  }

#define BINTREE_FIRST(res)                                                     \
  if (res == NULL) {                                                           \
    res = NULL;                                                                \
  } else {                                                                     \
    while (BINTREE_HAS_LEFT(res)) {                                            \
      res = BINTREE_LEFT(res);                                                 \
    }                                                                          \
  }

#define BINTREE_NEXT(res)                                                      \
  if (res == NULL) {                                                           \
    res = NULL;                                                                \
  } else {                                                                     \
    if (BINTREE_HAS_RIGHT(res)) {                                              \
      res = BINTREE_RIGHT(res);                                                \
      BINTREE_FIRST(res)                                                       \
    } else {                                                                   \
      while (BINTREE_HAS_PARENT(res) &&                                        \
             res == BINTREE_RIGHT(BINTREE_PARENT(res)))                        \
        res = BINTREE_PARENT(res);                                             \
      res = BINTREE_PARENT(res);                                               \
    }                                                                          \
  }

#define BINTREE_INSERT(parent, entry)                                          \
  if (parent != NULL) {                                                        \
    if (!BINTREE_HAS_LEFT(parent)) {                                           \
      BINTREE_LEFT(parent) = calloc(1, sizeof(*(parent)));                     \
      BINTREE_PARENT(BINTREE_LEFT(parent)) = parent;                           \
      BINTREE_VALUE(BINTREE_LEFT(parent)) = entry;                             \
    } else {                                                                   \
      BINTREE_RIGHT(parent) = calloc(1, sizeof(*(parent)));                    \
      BINTREE_PARENT(BINTREE_RIGHT(parent)) = parent;                          \
      BINTREE_VALUE(BINTREE_RIGHT(parent)) = entry;                            \
    }                                                                          \
  }

#define BINTREE_REMOVE(node)                                                   \
  if (BINTREE_HAS_PARENT(node)) {                                              \
    if (BINTREE_LEFT(BINTREE_PARENT(node)) == node) {                          \
      BINTREE_LEFT(BINTREE_PARENT(node)) = NULL;                               \
    } else {                                                                   \
      BINTREE_RIGHT(BINTREE_PARENT(node)) = NULL;                              \
    }                                                                          \
    BINTREE_PARENT(node) = NULL;                                               \
  }

#define BINTREE_SET_ROOT(tree, value)                                          \
  (tree)->root = calloc(1, sizeof(*(tree)->root));                             \
  BINTREE_VALUE((tree)->root) = value;

#define BINTREE_FIND(tree, needle, res)                                        \
  {                                                                            \
    res = BINTREE_ROOT(tree);                                                  \
    BINTREE_FIRST(res);                                                        \
    bool found = false;                                                        \
    while (res != NULL) {                                                      \
      if (BINTREE_VALUE(res) == needle) {                                      \
        found = true;                                                          \
        break;                                                                 \
      }                                                                        \
      BINTREE_NEXT(res);                                                       \
    }                                                                          \
    res = found ? res : NULL;                                                  \
  }
#endif

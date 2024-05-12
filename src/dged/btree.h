#ifndef _BTREE_H
#define _BTREE_H

#include "vec.h"

#include <stdlib.h>

/** @file btree.h
 * Generic binary tree implementation.
 */

/**
 * Define the entry type for the binary tree.
 */
#define BINTREE_ENTRY_TYPE(name, entry)                                        \
  struct name {                                                                \
    struct name *parent;                                                       \
    struct name *left;                                                         \
    struct name *right;                                                        \
    entry value;                                                               \
  }

/**
 * Declare a binary tree.
 */
#define BINTREE(entry)                                                         \
  struct {                                                                     \
    struct entry *root;                                                        \
  }

/**
 * Initialize the binary tree.
 *
 * @param tree The binary tree to initialize.
 */
#define BINTREE_INIT(tree) ((tree)->root = NULL)

/**
 * Destroy the binary tree.
 *
 * Note that this does not clean up any resources and
 * you should call @ref BINTREE_FREE_NODES before calling this.
 */
#define BINTREE_DESTROY(tree, entry_type) BINTREE_INIT(tree)

/**
 * Get the root of the binary tree.
 *
 * @param tree The binary tree to get root for.
 */
#define BINTREE_ROOT(tree) (tree)->root

/**
 * Get the left child of the node.
 *
 * This can be NULL.
 * @param node The node to get left child for.
 */
#define BINTREE_LEFT(node) (node)->left

/**
 * Get the right child of the node.
 *
 * This can be NULL.
 * @param node The node to get right child for.
 */
#define BINTREE_RIGHT(node) (node)->right

/**
 * Get the parent node for this node.
 *
 * Can be NULL if node is root.
 * @param node The node to get parent node of.
 */
#define BINTREE_PARENT(node) (node)->parent

/**
 * Get the value for a binary tree node.
 *
 * @param node The node to get value for
 */
#define BINTREE_VALUE(node) (node)->value

/**
 * Check if this node has a parent node.
 *
 * @param node The node to check.
 */
#define BINTREE_HAS_PARENT(node) ((node)->parent != NULL)

/**
 * Check if this node has a left child.
 *
 * @param node The node to check.
 */
#define BINTREE_HAS_LEFT(node) ((node)->left != NULL)

/**
 * Check if this node has a right child.
 *
 * @param node The node to check.
 */
#define BINTREE_HAS_RIGHT(node) ((node)->right != NULL)

/**
 * Free any resources associated with @p node
 *
 * @param node The node to free resources for
 */
#define BINTREE_FREE_NODE(node) free(node)

/**
 * Free all nodes from @p root, downwards.
 *
 * @param root The node to start freeing at.
 * @param entry_type The type of entries in this tree.
 */
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

/**
 * Get the first (leftmost) node in the tree.
 *
 * @param res The result of the operation.
 */
#define BINTREE_FIRST(res)                                                     \
  if (res == NULL) {                                                           \
    res = NULL;                                                                \
  } else {                                                                     \
    while (BINTREE_HAS_LEFT(res)) {                                            \
      res = BINTREE_LEFT(res);                                                 \
    }                                                                          \
  }

/**
 * Get the next binary tree node.
 *
 * @param res The result of the operation.
 */
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

/**
 * Insert a node in the bintree as a child to @p parent.
 *
 * If the parent has no children, the new node is inserted
 * as the left child, otherwise it is inserted as the right.
 * @param parent The parent node of the new node.
 * @param entry The new node to insert.
 */
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

/**
 * Remove @p node from the tree.
 *
 * @param node The node to remove.
 */
#define BINTREE_REMOVE(node)                                                   \
  if (BINTREE_HAS_PARENT(node)) {                                              \
    if (BINTREE_LEFT(BINTREE_PARENT(node)) == node) {                          \
      BINTREE_LEFT(BINTREE_PARENT(node)) = NULL;                               \
    } else {                                                                   \
      BINTREE_RIGHT(BINTREE_PARENT(node)) = NULL;                              \
    }                                                                          \
    BINTREE_PARENT(node) = NULL;                                               \
  }

/**
 * Set the root of the binary tree.
 *
 * @param tree The tree to set the root for.
 * @param value The value entry for the root node.
 */
#define BINTREE_SET_ROOT(tree, value)                                          \
  (tree)->root = calloc(1, sizeof(*(tree)->root));                             \
  BINTREE_VALUE((tree)->root) = value;

/**
 * Find a specific value in the binary tree.
 *
 * @param tree The tree to search in.
 * @param needle The value to search for (will be compared with ==).
 * @param res A var to store the result in, will be NULL if not found.
 */
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

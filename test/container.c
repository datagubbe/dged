#include <stdint.h>

#include "dged/btree.h"

#include "assert.h"
#include "test.h"

void test_empty_bintree() {
  BINTREE_ENTRY_TYPE(node, int);
  BINTREE(node) tree;

  BINTREE_INIT(&tree);

  ASSERT(BINTREE_ROOT(&tree) == NULL,
         "Expected root of tree to be NULL initially");
  struct node *res = BINTREE_ROOT(&tree);
  BINTREE_NEXT(res);
  ASSERT(res == NULL, "Expected there to be no \"next\" initially");
  struct node *n = BINTREE_ROOT(&tree);
  BINTREE_FIRST(n);
  bool loop_empty = true;
  while (n != NULL) {
    loop_empty = false;
    BINTREE_NEXT(n);
  }

  ASSERT(loop_empty, "Expected an empty tree to yield an empty loop");

  BINTREE_FREE_NODES(BINTREE_ROOT(&tree), node);
}

void test_bintree_iter() {
  BINTREE_ENTRY_TYPE(node, char);
  BINTREE(node) tree;
  BINTREE_INIT(&tree);

  // insert at the root
  BINTREE_SET_ROOT(&tree, 'a');

  struct node *root = BINTREE_ROOT(&tree);
  ASSERT(BINTREE_VALUE(root) == 'a', "Expected root to have its value");

  // insert first child (left)
  BINTREE_INSERT(root, 'b');
  ASSERT(BINTREE_VALUE(root->left) == 'b',
         "Expected first child to have its value");

  // insert second child
  BINTREE_INSERT(root, 'c');
  ASSERT(BINTREE_VALUE(root->right) == 'c',
         "Expected second child to have its value");

  // insert first child of second child
  struct node *right = BINTREE_RIGHT(root);
  BINTREE_INSERT(right, 'd');

  // iterate
  char chars[4] = {0};
  uint32_t nchars = 0;
  struct node *n = BINTREE_ROOT(&tree);
  BINTREE_FIRST(n);
  while (n != NULL) {
    chars[nchars] = BINTREE_VALUE(n);
    ++nchars;
    BINTREE_NEXT(n);
  }

  ASSERT(nchars == 4, "Expected tree to have 4 nodes after insertions");
  ASSERT(chars[0] == 'b' && chars[1] == 'a' && chars[2] == 'd' &&
             chars[3] == 'c',
         "Expected tree to have been traversed correctly");

  // find
  struct node *res = NULL;
  BINTREE_FIND(&tree, 'b', res);
  ASSERT(res != NULL && res == BINTREE_LEFT(root),
         "Expected existing value to be found");
  ASSERT(BINTREE_VALUE(res) == 'b',
         "Expected found node to contain the searched for value");

  // remove
  struct node *to_remove = BINTREE_LEFT(right);
  BINTREE_REMOVE(to_remove);

  nchars = 0;
  n = BINTREE_ROOT(&tree);
  BINTREE_FIRST(n);
  while (n != NULL) {
    ++nchars;
    BINTREE_NEXT(n);
  }

  ASSERT(nchars == 3, "Expected three nodes to remain after removing one");
  BINTREE_FREE_NODES(to_remove, node);

  BINTREE_FREE_NODES(root, node);
}

void run_container_tests() {
  run_test(test_empty_bintree);
  run_test(test_bintree_iter);
}

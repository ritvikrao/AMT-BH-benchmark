#ifndef __TRAVERSAL_DECLS_H__
#define __TRAVERSAL_DECLS_H__

#include "Node.h"
#include "Particle.h"
#include "Worker.h"

#include "State.h"

class DataManager;
template<typename T> class CutoffWorker;

template<typename T>
class Traversal {

  // to request particles/nodes from
  DataManager *dm;

  void gravityLeaf(Node<T> *leaf, TraversalWorker *worker, State *state, uint64_t mask);

  public:

  Traversal() : dm(NULL) {}

  void setDataManager(DataManager *mgr){
    dm = mgr;
  }

  // preorder does not have good cache behavior, because
  // all children are stored together but processed
  // separately
  void preorderTraversal(Node<T> *root, CutoffWorker<T> *worker);
  // The gravity walk, for the group of buckets in the worker's context, of
  // which those in mask are walking (WalkGroup.h).
  void gravityWalk(Node<T> *root, TraversalWorker *worker, State *state, uint64_t mask);
  void topDownTraversal_local(Node<T> *root, CutoffWorker<T> *worker);
  void postorderTraversal(Node<T> *root, CutoffWorker<T> *worker);
};
#endif // __TRAVERSAL_DECLS_H__



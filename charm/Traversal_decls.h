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

  template<class W> void processLeaf(Node<T> *node, W *worker, State *state);

  public:

  Traversal() : dm(NULL) {}

  void setDataManager(DataManager *mgr){
    dm = mgr;
  }

  // preorder does not have good cache behavior, because
  // all children are stored together but processed
  // separately
  void preorderTraversal(Node<T> *root, CutoffWorker<T> *worker);
  // The gravity walk. W is the worker's static type: TreePiece passes its
  // LocalTraversalWorker or RemoteTraversalWorker (final classes), so every
  // per-node work() call is direct and inlined; Request, resuming a walk after
  // a fetch, only has a CutoffWorker and calls through the vtable.
  template<class W> void topDownTraversal(Node<T> *root, W *worker, State *state);
  void topDownTraversal_local(Node<T> *root, CutoffWorker<T> *worker);
  void postorderTraversal(Node<T> *root, CutoffWorker<T> *worker);
};
#endif // __TRAVERSAL_DECLS_H__



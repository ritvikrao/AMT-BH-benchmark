#ifndef __TRAVERSAL_DEFS_H__
#define __TRAVERSAL_DEFS_H__

#include "DataManager.h"
template<typename T>
void Traversal<T>::preorderTraversal(Node<T> *root, CutoffWorker<T> *worker){
  stack<Node<T>*> stk;
  stk.push(root);

  while(!stk.empty()){
    Node<T> *t_node = stk.top();
    stk.pop();

    int ret = worker->work(t_node);
    if(ret > 0){
      Node<T> *t_children = t_node->getChildren();
      for(int i = t_node->getNumChildren()-1; i >= 0; i--){
        stk.push(t_children+i);
      }
    }
  }
}

template<typename T>
void Traversal<T>::topDownTraversal_local(Node<T> *root, CutoffWorker<T> *worker){
  stack<Node<T>*> ptrStk;
  int ret = worker->work(root);
  if(ret == 0) return;

  ptrStk.push(root); 

  Node<T> *tmp[BRANCH_FACTOR];
  int numChildrenToExpand;

  while(!ptrStk.empty()){
    Node<T> *t_parent = ptrStk.top();
    Node<T> *t_children = t_parent->getChildren();
    int t_numChildren = t_parent->getNumChildren();
    ptrStk.pop();

    numChildrenToExpand = 0;
    for(int i = 0; i < t_numChildren; i++){
      Node<T> *t_node = t_children+i;
      ret = worker->work(t_node);
      if(ret > 0 && t_node->getNumChildren() > 0){
        tmp[numChildrenToExpand] = t_node;
        numChildrenToExpand++;
      } 
    }
    for(int i = numChildrenToExpand-1; i >= 0; i--){
      ptrStk.push(tmp[i]);
    }
  }
}

template<typename T>
void Traversal<T>::gravityWalk(Node<T> *root, TraversalWorker *worker, State *state, uint64_t mask){
  uint64_t open = worker->visit(root,mask);
  if(open == 0) return;

  // An explicit stack in a fixed array, of nodes and the buckets that opened
  // them: a std::stack (a deque) costs a call and a branch per push and pop,
  // and this loop runs for every node the walk meets. Keys have 64 bits, so
  // the tree is at most 64 levels deep, and the walk holds at most
  // BRANCH_FACTOR - 1 waiting siblings per level.
  const int STACK_MAX = 64*BRANCH_FACTOR + 1;
  Node<T> *nodeStk[STACK_MAX];
  uint64_t maskStk[STACK_MAX];
  int top = 0;
  nodeStk[top] = root;
  maskStk[top++] = open;

  Node<T> *tmpNode[BRANCH_FACTOR];
  uint64_t tmpMask[BRANCH_FACTOR];

  while(top > 0){
    --top;
    Node<T> *t_parent = nodeStk[top];
    const uint64_t t_mask = maskStk[top];
    Node<T> *t_children = t_parent->getChildren();
    int t_numChildren = t_parent->getNumChildren();

    if(t_numChildren == 0){
      gravityLeaf(t_parent,worker,state,t_mask);
      continue;
    }

    int numChildrenToExpand = 0;
    for(int i = 0; i < t_numChildren; i++){
      Node<T> *t_node = t_children+i;
      uint64_t m = worker->visit(t_node,t_mask);
      if(m != 0){
        tmpNode[numChildrenToExpand] = t_node;
        tmpMask[numChildrenToExpand] = m;
        numChildrenToExpand++;
      }
    }
    CkAssert(top + numChildrenToExpand <= STACK_MAX);
    for(int i = numChildrenToExpand-1; i >= 0; i--){
      nodeStk[top] = tmpNode[i];
      maskStk[top++] = tmpMask[i];
    }
  }
}

template<typename T>
void Traversal<T>::gravityLeaf(Node<T> *leaf, TraversalWorker *worker, State *state, uint64_t mask){
  NodeType type = leaf->getType();

  CkAssert(type != Internal);
  CkAssert(type != Boundary);

  if(type == EmptyBucket || type == RemoteEmptyBucket) return;

  WalkContext here(worker->getContext().group,mask);
  if(type == Bucket){
    Particle *particles = leaf->getParticles();
    int np = leaf->getNumParticles();
    CkAssert(particles);
    CkAssert(np > 0);
    worker->applyParticles(particles,np,mask);
  }
  else if(type == RemoteBucket){
    ExternalParticle *particles = (ExternalParticle *)leaf->getParticles();
    int np = leaf->getNumParticles();
    if(particles == NULL){
      state->incrPending();
      dm->requestParticles(leaf,worker,state,this,here);
      return;
    }
    worker->applyParticles(particles,np,mask);
  }
  else if(type == Remote){
    state->incrPending();
    dm->requestNode(leaf,worker,state,this,here);
  }
}

template<typename T>
void Traversal<T>::postorderTraversal(Node<T> *root, CutoffWorker<T> *worker){
  // value returned from work() is ignored
  // since it doesn't make sense in a postorder
  // traversal
  stack<Node<T>*> cstk;
  stack<Node<T>*> nstk;

  cstk.push(root);
  nstk.push(root);

  while(!cstk.empty()){
    Node<T> *c_node = cstk.top();
    Node<T> *n_node = nstk.top();

    // we have processed the children of
    // n_node, but not n_node itself
    if(c_node != n_node){
      worker->work(n_node);
      nstk.pop();
      continue;
    }

    // here, we have a node whose children
    // haven't yet been processed

    cstk.pop();
    // first check whether the node has children
    if(c_node->getNumChildren() > 0){
      // we must explore the children of this node
      for(int i = c_node->getNumChildren()-1; i >= 0; i--){
        Node<T> *child = c_node->getChild(i);
        int num = child->getNumChildren();
        cstk.push(child);
        nstk.push(child);
      }
    } else {
      // this node does not have any children
      // so it is ok to visit it
      worker->work(n_node);
      nstk.pop();
    }
  }

  while(!nstk.empty()){
    worker->work(nstk.top());
    nstk.pop();
  }
}

#endif // __TRAVERSAL_DEFS_H__


#ifndef __REQUEST_H__
#define __REQUEST_H__

#include "Node.h"
#include "WalkGroup.h"


class TraversalWorker;
class State;
template<typename T> class Traversal;

struct Requestor {
  TraversalWorker *worker;
  State *state;
  Traversal<ForceData> *traversal;
  // The walk to resume: its group and the buckets that reached the leaf.
  WalkContext context;

  Requestor(TraversalWorker *w, State *s, Traversal<ForceData> * t, const WalkContext &ctx) :
    worker(w),
    state(s),
    traversal(t),
    context(ctx)
  {
  }

  Requestor() : 
    worker(NULL), state(NULL), traversal(NULL)
  {
  }
};

struct Request {
  // don't need data field
  void *data;
  bool sent;
  CkVec<Requestor> requestors;
  void *msg;

  Node<ForceData> *parent; 
  bool parentCached;

  Request() : 
    data(NULL),
    sent(false),
    msg(NULL),
    parent(NULL)
  {
  }

  void reset(){
    data = NULL;
    sent = false;
  }

  void deliverParticles(int num);
  void deliverNode();
};

#endif

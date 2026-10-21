#ifndef __WORKER_H__
#define __WORKER_H__

#include "Node.h"
#include "util.h"
#include "MultipoleMoments.h"
#include "Descriptor.h"
#include "State.h"
#include "gravity.h"

#include <map>
using namespace std;

// worker abstract class
template<typename T>
class CutoffWorker {
  public:
  virtual int work(Node<T> *) = 0; 
  virtual void work(ExternalParticle *) {}
  virtual void bucketDone(Key k) {}
  virtual void *getContext() {return NULL;}
  virtual void setContext(void *context) {}
  virtual void done() {}
};

class DataManager;
class ParticleFlushWorker : public CutoffWorker<NodeDescriptor> {
  int leafCnt;
  DataManager *dataManager;

  public:
  ParticleFlushWorker(DataManager *dm) : 
    dataManager(dm),
    leafCnt(0)
  {
  }

  int work(Node<NodeDescriptor> *node);
  int getNumLeaves(){
    return leafCnt;
  }
};

class MomentsWorker : public CutoffWorker<ForceData> {

  // tree pieces on this PE, in sorted order
  // this allows us to mark the nodes that are
  // internal to each PE. The last PE in this list
  // is a dummy value, and is equal to the number of
  // useful treepieces+1
  CkVec<TreePieceDescriptor> &peTreePieces;
  map<Key,Node<ForceData>*> &nodeTable;
  CkVec<Node<ForceData>*> &buckets;
  // This step's particle count per TreePiece, from the decomposition: which
  // owners can answer for a node (servingOwner).
  const int *tpCounts;
  // Remote leaves with no particles anywhere, left for the DataManager to
  // settle once the walk is done.
  CkVec<Node<ForceData>*> &emptyRemoteLeaves;

  int curTP;

  void requestMomentsOf(Node<ForceData> *leaf);

  public:
  MomentsWorker(CkVec<TreePieceDescriptor> &petps,
                map<Key,Node<ForceData>*> &tab,
                CkVec<Node<ForceData>*> &bucks,
                const int *counts,
                CkVec<Node<ForceData>*> &empties
                ) :
    peTreePieces(petps),
    nodeTable(tab),
    buckets(bucks),
    tpCounts(counts),
    emptyRemoteLeaves(empties),
    curTP(0)
  {
  }

  int work(Node<ForceData> *node);
  void setLeafType(Node<ForceData> *leaf);
  void setTypeFromChildren(Node<ForceData> *node);
};

#ifdef CHECK_TRAVERSAL_MASS
#include "common.h"
// Every bucket must end a step having interacted with the entire system, once:
// the mass it sees directly plus the mass of every node it approximated should
// come to the total mass, identically for every bucket on every PE. It is the
// invariant that catches an interaction being dropped or applied twice, which
// energies alone only hint at. Keyed by bucket; one map per PE, which under
// Reconverse is one per worker thread -- so no cap on the PE count (it used to
// be an array indexed by CkMyPe(), overrun past 1024 PEs).
extern thread_local std::map<Key,Real> traversalMass;
#endif

class State;
class TraversalWorker : public CutoffWorker<ForceData> {
  protected:
  TreePiece *ownerTreePiece;
  State *state;
  
  Node<ForceData> *currentBucket;

  // Which node types this walk handles (indexed by NodeType), and whether it
  // applies an unopened Boundary node. Both traversals keep Boundary nodes --
  // a Boundary node is the shared spine between the local and the remote part
  // of the tree, so each has to be able to walk down through it. When such a
  // node is *not* opened, though, its multipole stands for its whole subtree,
  // local and remote alike, and only one of the two may apply it: the local
  // traversal does. Data rather than virtual functions, so that work() --
  // called for every node the walk meets -- makes no calls of its own.
  const bool *keep;
  bool appliesUnopenedBoundary;

  TraversalWorker(const bool *k, bool unopenedBoundary) : 
    ownerTreePiece(NULL),
    currentBucket(NULL),
    keep(k),
    appliesUnopenedBoundary(unopenedBoundary)
  {
  }

  public:

  void reset(TreePiece *owner, State *s, Node<ForceData> *bucket){
    ownerTreePiece = owner;
    state = s;
    currentBucket = bucket;
  }

  Node<ForceData> *getCurrentBucket(){
    return currentBucket;
  }

  void *getContext(){
    return (void *) currentBucket;
  }

  void setContext(void *context){
    currentBucket = (Node<ForceData> *) context;
  }

  // Defined here, not in Worker.cpp, so that the walk (Traversal_defs.h),
  // instantiated for the final classes below, can inline them.
  int work(Node<ForceData> *node){
    NodeType type = node->getType();
    state->nodeEncountered(currentBucket->getKey(),node);

    if(!keep[type]){
      state->nodeDiscarded(currentBucket->getKey(),node);
      return 0;
    }

    // basic opening criterion
    bool open = openCriterionBucket(node,currentBucket);
    state->incrOpenCriterion();
    if(open){
      state->nodeOpened(currentBucket->getKey(),node);
      return 1;
    }

    // Applied by the local traversal only; see appliesUnopenedBoundary.
    if(type == Boundary && !appliesUnopenedBoundary){
      state->nodeDiscarded(currentBucket->getKey(),node);
      return 0;
    }

#ifdef CHECK_TRAVERSAL_MASS
    traversalMass[currentBucket->getKey()] += node->data.moments.totalMass;
#endif
    int computed = nodeBucketForce(node,currentBucket);
    state->nodeComputed(currentBucket,node->getKey());
    state->incrPartNodeInteractions(currentBucket->getKey(),computed);
    return 0;
  }

  void work(ExternalParticle *particle){
#ifdef CHECK_TRAVERSAL_MASS
    traversalMass[currentBucket->getKey()] += particle->mass;
#endif
    int computed = partBucketForce(particle,currentBucket);
    state->incrPartPartInteractions(currentBucket->getKey(),computed);
  }

  void bucketDone(Key k){
    state->bucketComputed(currentBucket,k);
  }
  
  virtual void done() {}
};

class LocalTraversalWorker final : public TraversalWorker {
  static const bool keepTable[];
  public:
  LocalTraversalWorker() : TraversalWorker(keepTable, true) {}
};

class RemoteTraversalWorker final : public TraversalWorker {
  static const bool keepTable[];
  public:
  RemoteTraversalWorker() : TraversalWorker(keepTable, false) {}
  void done();
};

class TreeSizeWorker : public CutoffWorker<ForceData> {

  int numNodes;
  int cutoffDepth;

  public:
  TreeSizeWorker(int d) : 
    cutoffDepth(d),
    numNodes(0)
  {
  }

  int work(Node<ForceData> *node);
  int getNumNodes(){
    return numNodes;
  }
};

template<typename T>
class FreeTreeWorker : public CutoffWorker<T> {
  public:
  int work(Node<T> *node){
    if(node->getNumChildren() > 0){
      /*
         CkPrintf("(%d) deleting %d children of %lu ptr %lx\n", 
         CkMyPe(), 
         node->getNumChildren(),
         node->getKey(),
         node->getChildren()
         );
         */
      delete[] node->getChildren();
    }
    return 1;
  }
};

class TreeChecker : public CutoffWorker<ForceData> {
  ostream &os;
  public: 
  TreeChecker(ostream &o) : os(o) {}
  int work(Node<ForceData> *node);
};


#endif

#ifndef __WORKER_H__
#define __WORKER_H__

#include "Node.h"
#include "util.h"
#include "MultipoleMoments.h"
#include "Descriptor.h"
#include "State.h"
#include "gravity.h"
#include "WalkGroup.h"
#include "Packet.h"

#include <map>
#if defined(__AVX2__)
#include <immintrin.h>
#endif
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
// The gravity walk's worker. It walks for a group of buckets at a time
// (WalkGroup.h): visit() takes the buckets still walking and returns the ones
// that open the node; each of the others applies the node, unless it is a
// Boundary node this walk leaves to the other one.
class TraversalWorker {
  protected:
  TreePiece *ownerTreePiece;
  State *state;
  WalkContext ctx;

  // Which node types this walk handles (indexed by NodeType), and whether it
  // applies an unopened Boundary node. Both traversals keep Boundary nodes --
  // a Boundary node is the shared spine between the local and the remote part
  // of the tree, so each has to be able to walk down through it. When such a
  // node is *not* opened, though, its multipole stands for its whole subtree,
  // local and remote alike, and only one of the two may apply it: the local
  // traversal does. Data rather than virtual functions, so that visit() --
  // called for every node the walk meets -- makes no calls of its own.
  const bool *keep;
  bool appliesUnopenedBoundary;

  TraversalWorker(const bool *k, bool unopenedBoundary) : 
    ownerTreePiece(NULL),
    state(NULL),
    keep(k),
    appliesUnopenedBoundary(unopenedBoundary)
  {
  }

  public:
  virtual ~TraversalWorker() {}

  void reset(TreePiece *owner, State *s){
    ownerTreePiece = owner;
    state = s;
  }

  const WalkContext &getContext() const { return ctx; }
  TreePiece *getOwner() const { return ownerTreePiece; }
  // Starts a walk (or resumes one after a fetch) for c's group: binds the
  // interaction lists or loads the packet. endWalk() applies what the walk
  // gathered; every walk ends with it.
  void setContext(const WalkContext &c){
    ctx = c;
    if(c.group == NULL) return;
    if(c.group->packet) packetBind(*c.group);
    else bindSources(*c.group);
  }
  void endWalk(){
    if(ctx.group != NULL && ctx.group->packet) packetFlush();
    else flushAllSources();
  }

  uint64_t visit(Node<ForceData> *node, uint64_t mask){
    NodeType type = node->getType();
    if(!keep[type]) return 0;

    const bool apply = (type != Boundary || appliesUnopenedBoundary);
    const Real mass = node->data.moments.totalMass;
    const Vector3D<Real> &cm = node->data.moments.cm;
    const WalkGroup &g = *ctx.group;

    // openCriterionBucket for every bucket of the group at once, with the
    // same arithmetic, so the same decisions: tolsq*|cm_node - cm_bucket|^2
    // < rsq opens the node for that bucket.
    const Real tolsq = globalParams.tolsq;
    const Real rsq = node->data.moments.rsq;
    uint64_t open = 0;
#if defined(__AVX2__) && defined(__FMA__)
    // Four buckets per compare, skipping fours with no bucket still walking.
    // fma(dz,dz,fma(dy,dy,dx*dx)) is what GCC makes of the scalar
    // dx*dx + dy*dy + dz*dz with FMA available, so the decisions are the
    // same as openCriterionBucket's (checked: equal interaction counts).
    const __m256d ncx = _mm256_set1_pd(cm.x), ncy = _mm256_set1_pd(cm.y), ncz = _mm256_set1_pd(cm.z);
    const __m256d vtol = _mm256_set1_pd(tolsq), vrsq = _mm256_set1_pd(rsq);
    for(int b = 0; b < g.numBuckets; b += 4){
      if(((mask >> b) & 0xF) == 0) continue;
      const __m256d dx = _mm256_sub_pd(ncx, _mm256_load_pd(g.cx + b));
      const __m256d dy = _mm256_sub_pd(ncy, _mm256_load_pd(g.cy + b));
      const __m256d dz = _mm256_sub_pd(ncz, _mm256_load_pd(g.cz + b));
      __m256d drsq = _mm256_mul_pd(dx, dx);
      drsq = _mm256_fmadd_pd(dy, dy, drsq);
      drsq = _mm256_fmadd_pd(dz, dz, drsq);
      const __m256d o = _mm256_cmp_pd(_mm256_mul_pd(vtol, drsq), vrsq, _CMP_LT_OQ);
      open |= (uint64_t)_mm256_movemask_pd(o) << b;
    }
#else
    for(uint64_t left = mask; left != 0; left &= left - 1){
      const int b = __builtin_ctzll(left);
      const Real dx = cm.x - g.cx[b];
      const Real dy = cm.y - g.cy[b];
      const Real dz = cm.z - g.cz[b];
      const Real drsq = dx*dx + dy*dy + dz*dz;
      if(tolsq*drsq < rsq) open |= (1ull << b);
    }
#endif
    open &= mask;
    state->incrOpenCriterion(__builtin_popcountll(mask));

    if(apply && g.packet){
      const uint64_t accepted = mask & ~open;
      if(accepted != 0){
#ifdef CHECK_TRAVERSAL_MASS
        for(uint64_t left = accepted; left != 0; left &= left - 1)
          traversalMass[g.buckets[__builtin_ctzll(left)]->getKey()] += mass;
#endif
        const uint64_t lanes = packetLanes(accepted);
        packetApply(lanes,mass,cm);
        state->incrPartNodeInteractions(0,__builtin_popcountll(lanes));
      }
    }
    else if(apply){
      for(uint64_t left = mask & ~open; left != 0; left &= left - 1){
        const int b = __builtin_ctzll(left);
#ifdef CHECK_TRAVERSAL_MASS
        traversalMass[g.buckets[b]->getKey()] += mass;
#endif
        addSource(b,mass,cm);
        state->incrPartNodeInteractions(0,g.np[b]);
      }
    }
    return open;
  }

  // A leaf's particles, reached (opened) by the buckets in mask. P is
  // Particle for a local bucket and ExternalParticle for a fetched one: the
  // array's own type, so that particles[i] steps by the right size.
  template<class P>
  void applyParticles(const P *particles, int np, uint64_t mask){
    Node<ForceData> **buckets = ctx.group->buckets;
    if(ctx.group->packet){
#ifdef CHECK_TRAVERSAL_MASS
      for(uint64_t left = mask; left != 0; left &= left - 1)
        for(int i = 0; i < np; i++)
          traversalMass[buckets[__builtin_ctzll(left)]->getKey()] += particles[i].mass;
#endif
      const uint64_t lanes = packetLanes(mask);
      for(int i = 0; i < np; i++) packetApply(lanes,particles[i].mass,particles[i].position);
      state->incrPartPartInteractions(0,(CmiUInt8)np*__builtin_popcountll(lanes));
      return;
    }
    for(uint64_t left = mask; left != 0; left &= left - 1){
      const int b = __builtin_ctzll(left);
      Node<ForceData> *bucket = buckets[b];
      for(int i = 0; i < np; i++){
#ifdef CHECK_TRAVERSAL_MASS
        traversalMass[bucket->getKey()] += particles[i].mass;
#endif
        addSource(b,particles[i].mass,particles[i].position);
      }
      state->incrPartPartInteractions(0,(CmiUInt8)np*bucket->getNumParticles());
    }
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

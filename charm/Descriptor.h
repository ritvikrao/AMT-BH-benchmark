#ifndef __DESCRIPTOR_H__
#define __DESCRIPTOR_H__

#include "defines.h"
#include "OrientedBox.h"
#include "MultipoleMoments.h"
#include "charm++.h"

struct BoundingBox {
  OrientedBox<Real> box;
  int numParticles;
  // Tracked separately so that the whitepaper's E_K and E_P (and their sum
  // E_T, which should stay constant) can each be reported.
  Real kineticEnergy;
  Real potentialEnergy;

  BoundingBox(){
    reset();
  }

  void reset(){
    numParticles = 0;
    box.reset();
    kineticEnergy = 0.0;
    potentialEnergy = 0.0;
  }

  Real totalEnergy() const { return kineticEnergy + potentialEnergy; }

  void grow(const Vector3D<Real> &v){
    box.grow(v);
  }

  void grow(const BoundingBox &other){
    if(other.numParticles == 0) return;
    if(numParticles == 0){
      *this = other;
    }
    else{
      box.grow(other.box);
      numParticles += other.numParticles;
      kineticEnergy += other.kineticEnergy;
      potentialEnergy += other.potentialEnergy;
    }
  }

  // Grow the box outward by `pad` times its own extent in each dimension.
  //
  // NOTE: the original scaled each corner by (1 +/- pad). For a corner with a
  // negative coordinate that moves the corner *inward*, shrinking the box, so
  // particles on the boundary could end up outside the universe and hash to
  // out-of-range Morton keys. Padding by a fraction of the size is symmetric
  // and cannot shrink the box.
  void expand(Real pad){
    Vector3D<Real> delta = (box.greater_corner - box.lesser_corner)*pad;
    // A degenerate axis (all particles coplanar, or a single particle) would
    // leave a zero-width box and divide by zero when hashing coordinates.
    if(delta.x <= 0.0) delta.x = pad;
    if(delta.y <= 0.0) delta.y = pad;
    if(delta.z <= 0.0) delta.z = pad;
    box.lesser_corner -= delta;
    box.greater_corner += delta;
  }

  void pup(PUP::er &p){
    p | box;
    p | numParticles;
    p | kineticEnergy;
    p | potentialEnergy;
  }

};

// how many particles do I have under this node?
// what are the largest and smallest keys among them?
struct NodeDescriptor {
  int numParticles;
  Key nodeKey;

  Key smallestKey;
  Key largestKey;

  // Sum of the particles' costs (Particle::cost). Filled in for the
  // decomposition histogram only; 0 elsewhere.
  double cost;

  NodeDescriptor() {
    numParticles = 0;
    largestKey = Key(0);
    smallestKey = ~largestKey;
    nodeKey = smallestKey;
    cost = 0.0;
  }

  NodeDescriptor(int np, Key nk, Key sk, Key lk, double c = 0.0) : 
    numParticles(np), 
    nodeKey(nk), smallestKey(sk), largestKey(lk), cost(c)
  {
  }

 void grow(const NodeDescriptor &other){
   if(other.numParticles == 0){
      return;
    }

    if(numParticles == 0){
      smallestKey = other.smallestKey;
      largestKey = other.largestKey;
      numParticles = other.numParticles;
      cost = other.cost;
    } else {
      if(smallestKey > other.smallestKey) smallestKey = other.smallestKey;
      if(largestKey < other.largestKey) largestKey = other.largestKey;
      numParticles += other.numParticles;
      cost += other.cost;
    }

  }
};

class TreePiece;
struct ParticleMsg;
struct TreePieceDescriptor {
  CkVec<ParticleMsg*> *vec;
  TreePiece *owner;
  int index;
  int numParticles;
  Key smallestKey;
  Key largestKey;

  int bucketStartIdx;
  int bucketEndIdx;

  TreePieceDescriptor() : 
    vec(NULL), owner(NULL), numParticles(0), index(-1), smallestKey(~Key(0)), largestKey(Key(0))
  {
  }

  TreePieceDescriptor(CkVec<ParticleMsg*> *v, int np, TreePiece *o, int i, Key sk, Key lk) : 
    vec(v), owner(o), index(i), smallestKey(sk), largestKey(lk), numParticles(np)
  {
  }

  TreePieceDescriptor(int i) : 
    vec(NULL), owner(NULL), numParticles(0), index(i), smallestKey(~Key(0)), largestKey(Key(0))
  {
  }

  // Sorted by TreePiece index, which is the order the decomposition assigned
  // key ranges in and the order MomentsWorker::setLeafType walks them. Sorting
  // by received keys instead breaks for TreePieces that got no particles this
  // step, whose keys are the empty sentinels.
  bool operator<=(const TreePieceDescriptor &t){
    return index <= t.index;
  }
  bool operator>=(const TreePieceDescriptor &t){
    return index >= t.index;
  }
};

// The TreePiece to ask for a node's moments or children: one of the node's
// owners that holds particles, or -1 if none does.
//
// Any owner used to do, because every PE refined every node with more than one
// owner. Now a PE refines such a node only where it holds particles
// (DataManager::buildTree), so only the PE of an owner with particles is sure to
// have built it -- and every TreePiece's cell is a leaf of the same octree, so
// all of that owner's particles lie inside a node it shares with others. An
// owner with no particles may have nothing to answer from. Spread over the
// owners with particles, so that the nodes near the root, which nearly every
// PE asks for, are not all served by one PE.
//
// The spread is a hash of the node and the asking PE rather than rand(), whose
// choices are correlated across processes: every process starts rand() from
// the same seed and walks the same tree in much the same order. The hash is
// deterministic and differs per asker. It measured no faster (8M bodies, 4
// Stampede3 nodes, leaf 24): the busiest server's load comes from single-owner
// nodes many PEs need, which no choice of owner can spread.
inline int servingOwner(int ownerStart, int ownerEnd, const int *tpCounts, Key key){
  int n = ownerEnd-ownerStart+1;
  // splitmix64 finalizer over (key, PE)
  CmiUInt8 h = (CmiUInt8)key + 0x9E3779B97F4A7C15ULL*(CmiUInt8)(CkMyPe()+1);
  h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ULL;
  h = (h ^ (h >> 27)) * 0x94D049BB133111EBULL;
  h ^= h >> 31;
  int first = (int)(h%(CmiUInt8)n);
  for(int i = 0; i < n; i++){
    int tp = ownerStart+(first+i)%n;
    if(tpCounts[tp] > 0) return tp;
  }
  return -1;
}

struct ForceData {
  OrientedBox<Real> box;
  MultipoleMoments moments;

  ForceData() 
  {
  }
};

struct DtReductionStruct {
#ifdef STATISTICS 
  CmiUInt8 pnInteractions;
  CmiUInt8 ppInteractions;
  CmiUInt8 openCrit;
#endif
  Real vbya;
  bool haveNaN;
  // Work stealing (Steal.h): local walks offered and done by another PE.
  CmiUInt8 stealPublished;
  CmiUInt8 stealTaken;

  DtReductionStruct &operator+=(const DtReductionStruct &other){
    stealPublished += other.stealPublished;
    stealTaken += other.stealTaken;
#ifdef STATISTICS
    pnInteractions += other.pnInteractions;
    ppInteractions += other.ppInteractions;
    openCrit += other.openCrit;
#endif
    if(other.haveNaN) haveNaN = true;

    if(other.vbya < 0.0) {} 
    else if(vbya < 0.0) vbya = other.vbya;
    else if(other.vbya < vbya) vbya = other.vbya;

    return *this;
  }
};


#endif

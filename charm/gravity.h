#ifndef __GRAVITY_H__
#define __GRAVITY_H__

#include <cmath>
#include "Node.h"
#include "Vector3D.h"
#include "defines.h"
#include "Parameters.h"
#include "WalkGroup.h"

extern Parameters globalParams;

inline bool
openCriterionBucket(Node<ForceData> *node,
                   Node<ForceData> *bucketNode) {

  Vector3D<Real> dr = node->data.moments.cm - bucketNode->data.moments.cm;
  Real drsq = dr.lengthSquared();
  return (globalParams.tolsq*drsq < node->data.moments.rsq);
}

// Interaction lists. The walk does not apply a source (an unopened node's
// monopole, or a particle) to a bucket on the spot: it appends the source's
// mass and position to the bucket's list, and the bucket's particles are then
// run against the whole list at once (flushSources). That kernel keeps each
// target's sums in registers and vectorises over sources; applying sources
// one at a time stored every target's acceleration and potential back to
// memory after each interaction, and the next source had to wait for those
// stores.
//
// One list per bucket of a walk group (WalkGroup.h), per PE: a PE runs all of
// its TreePieces' walks on one thread, one group at a time. A walk binds the
// lists to its group's buckets (bindSources) and ends with flushAllSources() -- after each group (TreePiece::doLocalGravity,
// doRemoteGravity) and after each walk resumed by a fetch (Request) -- so the
// lists are empty between entry methods, and DataManager::finishIteration
// flushes once more before the PE reads the forces.
// List capacity. The four arrays of a slot are CAP doubles each, so a group of
// 32 buckets holds 32 x 4 x CAP x 8 bytes of lists, 128 KB at CAP 128 -- four
// times L1 -- and an append touches a line in each of the four arrays. A
// smaller CAP keeps the lists nearer the core and flushes more often, with a
// kernel of CAP sources per flush; -DSOURCE_CAP=n sets it (a multiple of 8).
#ifndef SOURCE_CAP
#define SOURCE_CAP 128
#endif
struct SourceLists {
  static const int CAP = SOURCE_CAP;
  // Per slot: how many sources wait, and the bucket they are for. Kept apart
  // from the coordinates so that an append reads one cache line.
  alignas(64) int n[WALK_GROUP_MAX_BUCKETS];
  Node<ForceData> *bucket[WALK_GROUP_MAX_BUCKETS];
  // Where slot b's sums go: the bucket's particles, or, for a walk another PE
  // does in the owner's place (Steal.h), that PE's private copy of them.
  Particle *target[WALK_GROUP_MAX_BUCKETS];
  alignas(64) Real x[WALK_GROUP_MAX_BUCKETS][CAP];
  alignas(64) Real y[WALK_GROUP_MAX_BUCKETS][CAP];
  alignas(64) Real z[WALK_GROUP_MAX_BUCKETS][CAP];
  alignas(64) Real m[WALK_GROUP_MAX_BUCKETS][CAP];
};
extern thread_local SourceLists sources;

inline void flushSources(int slot){
  SourceLists &s = sources;
  const int n = s.n[slot];
  if(n == 0) return;
  Particle *particles = s.target[slot];
  const int np = s.bucket[slot]->getNumParticles();
  const Real epssq = globalParams.epssq;
  const Real *__restrict sx = s.x[slot];
  const Real *__restrict sy = s.y[slot];
  const Real *__restrict sz = s.z[slot];
  const Real *__restrict sm = s.m[slot];

  for(int i = 0; i < np; i++){
    Particle &p = particles[i];
    const Real px = p.position.x, py = p.position.y, pz = p.position.z;
    Real ax = 0.0, ay = 0.0, az = 0.0, phi = 0.0;
#pragma omp simd reduction(+:ax,ay,az,phi)
    for(int j = 0; j < n; j++){
      const Real dx = sx[j] - px;
      const Real dy = sy[j] - py;
      const Real dz = sz[j] - pz;
      const Real drsq = dx*dx + dy*dy + dz*dz;
      // A particle meets itself at exactly zero distance: pairwise in its
      // own bucket, or as the monopole of a one-particle bucket (whose centre
      // of mass is its position). Eq. 12 sums over j != i, so it contributes
      // nothing. (Adding -m/eps and subtracting it after the walk, as this
      // code did until 2026-10, cancels catastrophically at small eps, and
      // at eps = 0 is m/0.) The acceleration term was mor3*0 anyway.
      // Computed unconditionally and then selected, so that the loop has no
      // branch and vectorises; at eps = 0 the discarded lane holds inf.
      const Real r = 1.0/std::sqrt(drsq + epssq);
      const Real rinv = (drsq > 0.0) ? r : 0.0;
      const Real mr = sm[j]*rinv;
      const Real mr3 = mr*rinv*rinv;
      phi -= mr;
      ax += mr3*dx;
      ay += mr3*dy;
      az += mr3*dz;
    }
    p.potential += phi;
    p.acceleration.x += ax;
    p.acceleration.y += ay;
    p.acceleration.z += az;
    p.cost += n;
  }
  s.n[slot] = 0;
}

inline void flushAllSources(){
  for(int b = 0; b < WALK_GROUP_MAX_BUCKETS; b++) flushSources(b);
}

// Slot b belongs to the group's bucket b from here until the walk's closing
// flushAllSources(). The lists are empty whenever a walk starts.
inline void bindSources(const WalkGroup &g){
  for(int b = 0; b < g.numBuckets; b++){
    CkAssert(sources.n[b] == 0);
    sources.bucket[b] = g.buckets[b];
    sources.target[b] = g.buckets[b]->getParticles();
  }
}

inline void addSource(int slot, Real mass, const Vector3D<Real> &position){
  SourceLists &s = sources;
  const int k = s.n[slot];
  s.x[slot][k] = position.x;
  s.y[slot][k] = position.y;
  s.z[slot][k] = position.z;
  s.m[slot][k] = mass;
  if((s.n[slot] = k + 1) == SourceLists::CAP) flushSources(slot);
}

#endif

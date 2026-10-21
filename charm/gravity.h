#ifndef __GRAVITY_H__
#define __GRAVITY_H__

#include <cmath>
#include "Node.h"
#include "Vector3D.h"
#include "defines.h"
#include "Parameters.h"

extern Parameters globalParams;

inline bool
openCriterionBucket(Node<ForceData> *node,
                   Node<ForceData> *bucketNode) {

  Vector3D<Real> dr = node->data.moments.cm - bucketNode->data.moments.cm;
  Real drsq = dr.lengthSquared();
  return (globalParams.tolsq*drsq < node->data.moments.rsq);
}

// Interaction list. The walk does not apply a source (an unopened node's
// monopole, or a particle) to its bucket on the spot: it appends the source's
// mass and position here, and the bucket's particles are then run against the
// whole list at once (flushSources). That kernel keeps each target's sums in
// registers and vectorises over sources; applying sources one at a time
// stored every target's acceleration and potential back to memory after each
// interaction, and the next source had to wait for those stores.
//
// One list per PE: a PE runs all of its TreePieces' walks on one thread, and
// the list belongs to one bucket at a time. It is flushed when a source for
// another bucket arrives, when it is full, and by flushSources() before the
// PE uses the forces (DataManager::traversalsDone), so no source is applied
// late or to the wrong bucket.
struct SourceList {
  static const int CAP = 256;
  alignas(64) Real x[CAP];
  alignas(64) Real y[CAP];
  alignas(64) Real z[CAP];
  alignas(64) Real m[CAP];
  int n;
  Node<ForceData> *bucket;
};
extern thread_local SourceList sourceList;

inline void flushSources(){
  SourceList &s = sourceList;
  const int n = s.n;
  if(n == 0) return;
  Particle *particles = s.bucket->getParticles();
  const int np = s.bucket->getNumParticles();
  const Real epssq = globalParams.epssq;
  const Real *__restrict sx = s.x;
  const Real *__restrict sy = s.y;
  const Real *__restrict sz = s.z;
  const Real *__restrict sm = s.m;

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
  s.n = 0;
}

inline void addSource(Node<ForceData> *bucket, Real mass, const Vector3D<Real> &position){
  SourceList &s = sourceList;
  if(s.bucket != bucket){
    flushSources();
    s.bucket = bucket;
  }
  const int k = s.n;
  s.x[k] = position.x;
  s.y[k] = position.y;
  s.z[k] = position.z;
  s.m[k] = mass;
  if((s.n = k + 1) == SourceList::CAP) flushSources();
}

inline
int nodeBucketForce(Node<ForceData> *node, 
		    Node<ForceData> *req){
  addSource(req,node->data.moments.totalMass,node->data.moments.cm);
  return req->getNumParticles();
}

inline int partBucketForce(ExternalParticle *part, 
			   Node<ForceData> *req){ 
  addSource(req,part->mass,part->position);
  return req->getNumParticles();
}

#endif

#ifndef __GRAVITY_H__
#define __GRAVITY_H__

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

inline 
void grav(Particle *pstart, Particle *pend, Real mass, const Vector3D<Real> &position){
  Vector3D<Real> dr;
  Real drsq;
  Real drabs;
  Real phii;
  Real mor3;

  for(Particle *p = pstart; p != pend; p++){
    dr = position - p->position;
    drsq = dr.lengthSquared();
    p->cost += 1.0;
    // A particle meets itself at exactly zero distance: pairwise in its own
    // bucket, or as the monopole of a one-particle bucket (whose centre of
    // mass is its position). Eq. 12 sums over j != i, so skip it. Adding
    // -m/eps here and subtracting it after the traversal, as this code did
    // until 2026-10, cancels catastrophically at small eps: at eps = 1e-11 a
    // heavy particle's m/eps is so large that every other contribution to its
    // potential rounds away, in an order that depends on the layout. At
    // eps = 0 it was m/0. The acceleration is unchanged: the term was mor3*0.
    if(drsq == 0.0) continue;
    drsq += globalParams.epssq;
    drabs = sqrt((double) drsq);
    phii = mass/drabs;
    p->potential -= phii;
    mor3 = phii/drsq;
    p->acceleration += mor3*dr;
  }
}

inline
int nodeBucketForce(Node<ForceData> *node, 
		    Node<ForceData> *req){
  
  Particle *particles = req->getParticles();
  int numParticles = req->getNumParticles();
  grav(particles,particles+numParticles,node->data.moments.totalMass,node->data.moments.cm);
  return req->getNumParticles();
}

inline int partBucketForce(ExternalParticle *part, 
			   Node<ForceData> *req){ 

  Particle *particles = req->getParticles();
  int numParticles = req->getNumParticles();
  grav(particles,particles+numParticles,part->mass,part->position);
  return numParticles;
}

#endif

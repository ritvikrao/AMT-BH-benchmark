#ifndef __PACKET_H__
#define __PACKET_H__

// The packet walk (-walk=packet): the particles of a walk group are SIMD
// lanes, and the walk applies each node a bucket accepts, and each particle
// of a leaf it reaches, to that bucket's lanes on the spot, four lanes per
// AVX2 operation. The sums stay in this per-PE packet until the walk ends
// (packetFlush), when they are added into the particles once. This is how
// the HPX and Julia-Dagger walks work, with one difference that keeps our
// results: the opening test is still made per bucket (TraversalWorker::visit),
// and its outcome applies to all of the bucket's lanes, so every bucket gets
// the same interactions as with the interaction lists (-walk=list,
// gravity.h), up to the order of summation.
//
// A packet holds the group's particles in bucket order, at most
// PACKET_MAX_LANES of them; a group with more uses the lists.

#include <cmath>
#include <cstdint>
#include "Node.h"
#include "Parameters.h"
#include "WalkGroup.h"
#if defined(__AVX2__)
#include <immintrin.h>
#endif

extern Parameters globalParams;

const int PACKET_MAX_LANES = 64;

struct Packet {
  alignas(64) Real px[PACKET_MAX_LANES];
  alignas(64) Real py[PACKET_MAX_LANES];
  alignas(64) Real pz[PACKET_MAX_LANES];
  alignas(64) Real ax[PACKET_MAX_LANES];
  alignas(64) Real ay[PACKET_MAX_LANES];
  alignas(64) Real az[PACKET_MAX_LANES];
  alignas(64) Real phi[PACKET_MAX_LANES];
  alignas(64) Real cost[PACKET_MAX_LANES];
  Particle *particle[PACKET_MAX_LANES];
  // Lane bits of each of the group's buckets.
  uint64_t bucketLanes[WALK_GROUP_MAX_BUCKETS];
  int numLanes;
  // Every bucket holds one particle (leaf 1): lane b is bucket b.
  bool lanePerBucket;
};
extern thread_local Packet packet;

// The lanes of the buckets in a bucket mask.
inline uint64_t packetLanes(uint64_t buckets){
  if(packet.lanePerBucket) return buckets;
  uint64_t lanes = 0;
  for(uint64_t left = buckets; left != 0; left &= left - 1)
    lanes |= packet.bucketLanes[__builtin_ctzll(left)];
  return lanes;
}

// Load the group's particles into the lanes. Lanes past the last particle,
// up to a multiple of four, get finite values; their bits are never set.
inline void packetBind(const WalkGroup &g){
  Packet &k = packet;
  int l = 0;
  bool onePerBucket = true;
  for(int b = 0; b < g.numBuckets; b++){
    Particle *p = g.buckets[b]->getParticles();
    const int np = g.buckets[b]->getNumParticles();
    if(np != 1) onePerBucket = false;
    const int first = l;
    for(int i = 0; i < np; i++, l++){
      k.px[l] = p[i].position.x;
      k.py[l] = p[i].position.y;
      k.pz[l] = p[i].position.z;
      k.particle[l] = p + i;
    }
    k.bucketLanes[b] = (np == 64) ? ~0ull : (((1ull << np) - 1) << first);
  }
  k.numLanes = l;
  // Not l == numBuckets: empty buckets and two-particle ones can balance out.
  k.lanePerBucket = onePerBucket;
  for(; l < PACKET_MAX_LANES; l++){
    k.px[l] = k.py[l] = k.pz[l] = 0.0;
  }
  for(l = 0; l < PACKET_MAX_LANES; l++){
    k.ax[l] = k.ay[l] = k.az[l] = k.phi[l] = k.cost[l] = 0.0;
  }
}

// Add the sums into the particles and clear them.
inline void packetFlush(){
  Packet &k = packet;
  for(int l = 0; l < k.numLanes; l++){
    Particle &p = *k.particle[l];
    p.acceleration.x += k.ax[l];
    p.acceleration.y += k.ay[l];
    p.acceleration.z += k.az[l];
    p.potential += k.phi[l];
    p.cost += k.cost[l];
    k.ax[l] = k.ay[l] = k.az[l] = k.phi[l] = k.cost[l] = 0.0;
  }
  k.numLanes = 0;
}

// One source (mass, position) applied to the lanes in `lanes`. The same
// arithmetic as the list kernel (gravity.h), self term included: a lane at
// exactly the source's position is the particle itself and gets nothing.
inline void packetApply(uint64_t lanes, Real mass, const Vector3D<Real> &pos){
  Packet &k = packet;
  const Real epssq = globalParams.epssq;
#if defined(__AVX2__) && defined(__FMA__)
  // Weights 1.0 for the lanes of a 4-bit mask, 0.0 for the others.
  alignas(32) static const double weight[16][4] = {
    {0,0,0,0},{1,0,0,0},{0,1,0,0},{1,1,0,0},{0,0,1,0},{1,0,1,0},{0,1,1,0},{1,1,1,0},
    {0,0,0,1},{1,0,0,1},{0,1,0,1},{1,1,0,1},{0,0,1,1},{1,0,1,1},{0,1,1,1},{1,1,1,1}};
  const __m256d sx = _mm256_set1_pd(pos.x), sy = _mm256_set1_pd(pos.y), sz = _mm256_set1_pd(pos.z);
  const __m256d sm = _mm256_set1_pd(mass), veps = _mm256_set1_pd(epssq);
  const __m256d one = _mm256_set1_pd(1.0), zero = _mm256_setzero_pd();
  // Only the fours of lanes with a lane set: an application usually covers a
  // few of the packet's lanes, so stepping through every four costs more
  // than the arithmetic.
  for(uint64_t left = lanes; left != 0; ){
    const int c = __builtin_ctzll(left) & ~3;
    left &= ~(0xFull << c);
    const unsigned bits = (unsigned)(lanes >> c) & 0xF;
    const __m256d w = _mm256_load_pd(weight[bits]);
    const __m256d dx = _mm256_sub_pd(sx, _mm256_load_pd(k.px + c));
    const __m256d dy = _mm256_sub_pd(sy, _mm256_load_pd(k.py + c));
    const __m256d dz = _mm256_sub_pd(sz, _mm256_load_pd(k.pz + c));
    __m256d drsq = _mm256_mul_pd(dx, dx);
    drsq = _mm256_fmadd_pd(dy, dy, drsq);
    drsq = _mm256_fmadd_pd(dz, dz, drsq);
    const __m256d r = _mm256_div_pd(one, _mm256_sqrt_pd(_mm256_add_pd(drsq, veps)));
    const __m256d rinv = _mm256_and_pd(r, _mm256_cmp_pd(drsq, zero, _CMP_GT_OQ));
    const __m256d mr = _mm256_mul_pd(_mm256_mul_pd(sm, w), rinv);
    const __m256d mr3 = _mm256_mul_pd(_mm256_mul_pd(mr, rinv), rinv);
    _mm256_store_pd(k.phi + c, _mm256_sub_pd(_mm256_load_pd(k.phi + c), mr));
    _mm256_store_pd(k.ax + c, _mm256_fmadd_pd(mr3, dx, _mm256_load_pd(k.ax + c)));
    _mm256_store_pd(k.ay + c, _mm256_fmadd_pd(mr3, dy, _mm256_load_pd(k.ay + c)));
    _mm256_store_pd(k.az + c, _mm256_fmadd_pd(mr3, dz, _mm256_load_pd(k.az + c)));
    _mm256_store_pd(k.cost + c, _mm256_add_pd(_mm256_load_pd(k.cost + c), w));
  }
#else
  for(uint64_t left = lanes; left != 0; left &= left - 1){
    const int l = __builtin_ctzll(left);
    const Real dx = pos.x - k.px[l];
    const Real dy = pos.y - k.py[l];
    const Real dz = pos.z - k.pz[l];
    const Real drsq = dx*dx + dy*dy + dz*dz;
    if(drsq == 0.0) continue;
    const Real rinv = 1.0/std::sqrt(drsq + epssq);
    const Real mr = mass*rinv;
    const Real mr3 = mr*rinv*rinv;
    k.phi[l] -= mr;
    k.ax[l] += mr3*dx;
    k.ay[l] += mr3*dy;
    k.az[l] += mr3*dz;
    k.cost[l] += 1.0;
  }
#endif
}

#endif

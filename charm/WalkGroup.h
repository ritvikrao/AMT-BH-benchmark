#ifndef __WALK_GROUP_H__
#define __WALK_GROUP_H__

#include <cstdint>
#include "Node.h"

// Buckets that walk the tree together. A walk per bucket meets the same
// upper nodes once for every bucket, and with one particle per bucket that
// was nearly all of the step (770M node visits per step at 400k bodies). A
// group of consecutive buckets of one TreePiece walks once instead: the
// opening test is still made for each bucket at every node it reaches, and a
// bucket that accepts a node leaves the walk below it, so each bucket gets
// exactly the interactions its own walk would have given it.
//
// A group holds at most WALK_GROUP_MAX_BUCKETS buckets (one bit each in the
// walk's mask) and at most globalParams.walkGroupParticles particles.
const int WALK_GROUP_MAX_BUCKETS = 32;

struct WalkGroup {
  Node<ForceData> **buckets;
  int numBuckets;
  // The buckets' centres of mass, by coordinate, so that the opening test
  // for all of the group's buckets against one node is a vector loop rather
  // than a pointer chase per bucket (TraversalWorker::visit).
  alignas(32) Real cx[WALK_GROUP_MAX_BUCKETS];
  alignas(32) Real cy[WALK_GROUP_MAX_BUCKETS];
  alignas(32) Real cz[WALK_GROUP_MAX_BUCKETS];
  int np[WALK_GROUP_MAX_BUCKETS];
};

// Where a group's walk stands: bit b of mask set means group->buckets[b] has
// opened every node above this point, so it is still walking.
struct WalkContext {
  const WalkGroup *group;
  uint64_t mask;
  WalkContext() : group(NULL), mask(0) {}
  WalkContext(const WalkGroup *g, uint64_t m) : group(g), mask(m) {}
};

inline uint64_t allBuckets(const WalkGroup &g){
  return (g.numBuckets == 64) ? ~0ull : ((1ull << g.numBuckets) - 1);
}

#endif

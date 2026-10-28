#ifndef __STEAL_H__
#define __STEAL_H__

// Work stealing of local gravity walks between the PEs of one process
// (-steal=1).
//
// A TreePiece's traversal is two walks per walk group (WalkGroup.h): the
// remote walk, which fetches other PEs' data and resumes in the owner's
// DataManager as replies arrive, and the local walk, which reads only this
// PE's own subtree -- built in the tree-build phase and never written during
// the traversal -- and issues no fetches. The local walks are what another PE
// of the same process can do in the owner's place: it walks the owner's tree
// through shared memory with its own interaction lists, and sums into a
// private copy of the group's particles. One message carries the sums back,
// and the owner adds them in (TreePiece::stolenLocalDone). So the owner stays
// the only writer of its particles, no lock guards them, and a local walk can
// be taken at any time -- the group's remote walk may still be running on
// the owner. (Letting the thief write the particles directly would need the
// remote walk to be finished first, and measured on Delta that condition
// starved the thieves: 90% of idle attempts found no such group.)
//
// Each TreePiece keeps a deque of the local walks not yet started. They are
// idle-time work for the whole process: a PE with nothing else to do
// (Converse's PROCESSOR_IDLE conditions) first takes one of its own
// TreePieces' walks, from the back, and does it in place; failing that it
// takes one from the front of some TreePiece of another PE. The owners do
// not run local walks from their message queues at all, so the remote walks
// drive the critical path and the local half fills the gaps -- a heavy PE's
// local walks stay available to the light ones instead of being consumed
// early by their owner. (Measured before this: thieves got 16% of the local
// walks, because the owners had done the rest by the time anyone was idle.) One lock per PE covers the deques of its
// TreePieces and the list of those that still have work. Stealing stays
// inside the process: the owner's tree is only addressable there.

#include "charm++.h"

class TreePiece;

// Per-PE counters, reported per step as [STEAL] (DataManager::advance).
struct StealStats {
  CmiUInt8 published;   // local walks this PE's TreePieces offered
  CmiUInt8 taken;       // walks this PE did for TreePieces of other PEs
  CmiUInt8 lost;        // walks of this PE's TreePieces done elsewhere
  // Idle callbacks that saw work queued in the process, and those among
  // them that took nothing (the queues emptied or were locked).
  CmiUInt8 attempts, empty;
  StealStats() : published(0), taken(0), lost(0), attempts(0), empty(0) {}
  void reset(){ published = taken = lost = attempts = empty = 0; }
};
extern thread_local StealStats stealStats;

// Once per PE, before the first traversal: the process-wide table and this
// PE's idle callbacks.
void stealInit();
// The owner offers its numGroups local walks (TreePiece::startTraversal).
void stealPublish(TreePiece *tp, int numGroups);

#endif

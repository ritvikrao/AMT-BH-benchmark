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
// through shared memory with its own interaction lists, and writes the group's
// particles directly, because by then nobody else writes them. The force
// kernels are the only writers of acceleration, potential and cost before
// integration, and a group's local walk is handed out only once its remote
// walk is quiet: the owner's pass over it is done and no request of its is in
// flight (State::groupQuiet). So no lock guards the particles; one message
// to the owner reports the group done.
//
// Each TreePiece keeps a deque of the local walks not yet started. The owner
// takes from the back, in doLocalGravity as before; an idle PE of the same
// process (Converse's PROCESSOR_IDLE conditions) takes a quiet group from the
// front of some TreePiece of another PE. One lock per PE covers the deques of
// its TreePieces and the list of those that still have work. Stealing stays
// inside the process: the owner's tree is only addressable there.

#include "charm++.h"

class TreePiece;

// Per-PE counters, reported per step as [STEAL] (DataManager::advance).
struct StealStats {
  CmiUInt8 published;   // local walks this PE's TreePieces offered
  CmiUInt8 taken;       // walks this PE did for TreePieces of other PEs
  CmiUInt8 lost;        // walks of this PE's TreePieces done elsewhere
  StealStats() : published(0), taken(0), lost(0) {}
  void reset(){ published = taken = lost = 0; }
};
extern thread_local StealStats stealStats;

// Once per PE, before the first traversal: the process-wide table and this
// PE's idle callbacks.
void stealInit();
// The owner offers its numGroups local walks (TreePiece::startTraversal).
void stealPublish(TreePiece *tp, int numGroups);
// The owner takes up to maxBuckets buckets' worth of its own walks from the
// back; returns how many groups, in out[]; more says whether any are left.
int stealPopOwn(TreePiece *tp, int maxBuckets, int *out, int cap, bool &more);

#endif

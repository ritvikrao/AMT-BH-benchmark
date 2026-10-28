#include "Steal.h"
#include "TreePiece.h"
#include "DataManager.h"
#include "Parameters.h"
#include "Worker.h"
#include "State.h"
#include "Traversal_decls.h"

#include <mutex>
#include <vector>
#include <deque>
#include <atomic>
#include "gravity.h"
#include "Packet.h"
#include "Timers.h"

extern CProxy_TreePiece treePieceProxy;
extern CProxy_DataManager dataManagerProxy;
extern Parameters globalParams;

thread_local StealStats stealStats;

namespace {

struct PeQueues {
  std::mutex lock;
  // TreePieces of this PE whose deque is not empty.
  std::vector<TreePiece *> pieces;
  // Set by the PE itself each step (stealSetHeavy); read by thieves.
  std::atomic<bool> heavy;
};

struct Table {
  int ranks;
  PeQueues *pes;
  // Local walks queued in the whole process: idle PEs look no further when
  // it is zero, which is all of the time outside the traversal phase.
  std::atomic<long> queued;
};

Table *table = NULL;
std::once_flag tableOnce;
thread_local bool registered = false;
thread_local unsigned rng = 0;
thread_local bool heavyPe = false;
thread_local int remoteOpen = 0;

// The thief's walk state. Thread-local, like the interaction lists it binds
// (gravity.h): a stolen walk is one PE's work from start to end.
thread_local LocalTraversalWorker thiefWorker;
thread_local State thiefState;
thread_local Traversal<ForceData> thiefTraversal;
// The thief's copy of the group's particles (positions and masses read, sums
// written), and the sums as sent back: ax, ay, az, phi, cost per particle.
thread_local std::vector<Particle> scratch;
thread_local std::vector<double> sums;

inline unsigned nextRand(){
  rng = rng * 1664525u + 1013904223u;
  return rng >> 8;
}

} // namespace

void stealRunStolen(TreePiece *tp, int g){
  PE_DIAG_TIME(busyLocal);
  const WalkGroup &group = tp->getGroup(g);
  const uint64_t all = allBuckets(group);
  // Private copy of the group's particles, sums cleared.
  int n = 0;
  for(int b = 0; b < group.numBuckets; b++) n += group.buckets[b]->getNumParticles();
  scratch.resize(n);
  int k = 0;
  for(int b = 0; b < group.numBuckets; b++){
    const Particle *p = group.buckets[b]->getParticles();
    const int np = group.buckets[b]->getNumParticles();
    for(int i = 0; i < np; i++, k++){
      scratch[k] = p[i];
      scratch[k].acceleration = Vector3D<Real>(0.0, 0.0, 0.0);
      scratch[k].potential = 0.0;
      scratch[k].cost = 0.0;
    }
  }
  thiefState.reset(tp, 1, NULL);
  thiefState.finishedIteration();
  thiefWorker.reset(tp, &thiefState);
  thiefWorker.setContext(WalkContext(&group, all));
  // Point the kernels' targets at the copy instead of the owner's particles.
  k = 0;
  for(int b = 0; b < group.numBuckets; b++){
    const int np = group.buckets[b]->getNumParticles();
    if(group.packet){
      for(int i = 0; i < np; i++) packet.particle[k + i] = &scratch[k + i];
    }
    else{
      sources.target[b] = &scratch[k];
    }
    k += np;
  }
  thiefTraversal.gravityWalk(tp->getRoot(), &thiefWorker, &thiefState, all);
  thiefWorker.endWalk();
  sums.resize(5*n);
  for(int i = 0; i < n; i++){
    sums[5*i+0] = scratch[i].acceleration.x;
    sums[5*i+1] = scratch[i].acceleration.y;
    sums[5*i+2] = scratch[i].acceleration.z;
    sums[5*i+3] = scratch[i].potential;
    sums[5*i+4] = scratch[i].cost;
  }
  stealStats.taken++;
  CmiUInt8 pn = 0, pp = 0, oc = 0;
#ifdef STATISTICS
  pn = thiefState.numInteractions[0];
  pp = thiefState.numInteractions[1];
  oc = thiefState.numInteractions[2];
#endif
  treePieceProxy[tp->getIndex()].stolenLocalDone(g, n, sums.data(), pn, pp, oc);
}

namespace {

inline CmiUInt8 handle(TreePiece *tp){ return (CmiUInt8)(uintptr_t)tp; }

// Idle callback: one local walk. This PE's own first (from the back, done in
// place), else one from the front of a TreePiece of another PE.
void stealIdle(void *){
  if(table == NULL || table->queued.load(std::memory_order_relaxed) == 0) return;
  const int me = CmiMyRank();
  const int n = table->ranks;
  // A heavy PE with its remote walk open keeps itself free for replies and
  // requests; its local walks are for the others.
  if(heavyPe && remoteOpen > 0) return;
  {
    PeQueues &pq = table->pes[me];
    std::unique_lock<std::mutex> lk(pq.lock);
    if(!pq.pieces.empty()){
      TreePiece *tp = pq.pieces.back();
      std::deque<int> &q = tp->stealQueue();
      const int g = q.back();
      q.pop_back();
      if(q.empty()) pq.pieces.pop_back();
      table->queued.fetch_sub(1, std::memory_order_relaxed);
      lk.unlock();
      // Through an inline entry method of this PE's DataManager, so that the
      // walk is traced: the idle callback itself is outside any entry method.
      dataManagerProxy[CkMyPe()].ownIdleWalk(handle(tp), g);
      return;
    }
  }
  stealStats.attempts++;
  const int start = nextRand() % n;
  // Two passes: heavy victims first, then the rest.
  for(int pass = 0; pass < 2; pass++)
  for(int t = 0; t < n; t++){
    const int r = (start + t) % n;
    if(r == me) continue;
    PeQueues &pq = table->pes[r];
    if((pass == 0) != pq.heavy.load(std::memory_order_relaxed)) continue;
    std::unique_lock<std::mutex> lk(pq.lock, std::try_to_lock);
    if(!lk.owns_lock()) continue;
    for(size_t i = 0; i < pq.pieces.size(); i++){
      TreePiece *tp = pq.pieces[i];
      std::deque<int> &q = tp->stealQueue();
      if(q.empty()) continue;
      const int g = q.front();
      q.pop_front();
      if(q.empty()){
        pq.pieces[i] = pq.pieces.back();
        pq.pieces.pop_back();
      }
      table->queued.fetch_sub(1, std::memory_order_relaxed);
      lk.unlock();
      dataManagerProxy[CkMyPe()].stolenWalk(handle(tp), g);
      return;
    }
  }
  stealStats.empty++;
}

} // namespace

void stealSetHeavy(bool heavy){
  heavyPe = heavy;
  if(table != NULL) table->pes[CmiMyRank()].heavy.store(heavy, std::memory_order_relaxed);
}
void stealRemoteOpened(){ remoteOpen++; }
void stealRemoteClosed(){ remoteOpen--; }

void stealInit(){
  std::call_once(tableOnce, [](){
    Table *t = new Table;
    t->ranks = CmiMyNodeSize();
    t->pes = new PeQueues[t->ranks];
    for(int r = 0; r < t->ranks; r++) t->pes[r].heavy.store(false);
    t->queued.store(0);
    table = t;
  });
  if(registered) return;
  registered = true;
  rng = (unsigned)CkMyPe() * 2654435761u + 12345u;
  if(CmiMyNodeSize() > 1){
    CcdCallOnConditionKeep(CcdPROCESSOR_BEGIN_IDLE, stealIdle, NULL);
    CcdCallOnConditionKeep(CcdPROCESSOR_STILL_IDLE, stealIdle, NULL);
  }
}

void stealPublish(TreePiece *tp, int numGroups){
  PeQueues &pq = table->pes[CmiMyRank()];
  std::lock_guard<std::mutex> lk(pq.lock);
  std::deque<int> &q = tp->stealQueue();
  q.clear();
  for(int g = 0; g < numGroups; g++) q.push_back(g);
  if(numGroups > 0) pq.pieces.push_back(tp);
  table->queued.fetch_add(numGroups, std::memory_order_relaxed);
  stealStats.published += numGroups;
}

#include "Traversal_defs.h"

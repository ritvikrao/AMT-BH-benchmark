#include "Steal.h"
#include "TreePiece.h"
#include "Parameters.h"
#include "Worker.h"
#include "State.h"
#include "Traversal_decls.h"

#include <mutex>
#include <vector>
#include <deque>
#include <atomic>

extern CProxy_TreePiece treePieceProxy;
extern Parameters globalParams;

thread_local StealStats stealStats;

namespace {

struct PeQueues {
  std::mutex lock;
  // TreePieces of this PE whose deque is not empty.
  std::vector<TreePiece *> pieces;
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

// The thief's walk state. Thread-local, like the interaction lists it binds
// (gravity.h): a stolen walk is one PE's work from start to end.
thread_local LocalTraversalWorker thiefWorker;
thread_local State thiefState;
thread_local Traversal<ForceData> thiefTraversal;

inline unsigned nextRand(){
  rng = rng * 1664525u + 1013904223u;
  return rng >> 8;
}

void runStolen(TreePiece *tp, int g){
  const WalkGroup &group = tp->getGroup(g);
  const uint64_t all = allBuckets(group);
  thiefState.reset(tp, 1, NULL);
  thiefState.finishedIteration();
  thiefWorker.reset(tp, &thiefState);
  thiefWorker.setContext(WalkContext(&group, all));
  thiefTraversal.gravityWalk(tp->getRoot(), &thiefWorker, &thiefState, all);
  thiefWorker.endWalk();
  stealStats.taken++;
  CmiUInt8 pn = 0, pp = 0, oc = 0;
#ifdef STATISTICS
  pn = thiefState.numInteractions[0];
  pp = thiefState.numInteractions[1];
  oc = thiefState.numInteractions[2];
#endif
  treePieceProxy[tp->getIndex()].stolenLocalDone(1, pn, pp, oc);
}

// Idle callback: take one quiet local walk from a TreePiece of another PE of
// this process, oldest first (the front: its remote walk started earliest).
void stealIdle(void *){
  if(table == NULL || table->queued.load(std::memory_order_relaxed) == 0) return;
  const int me = CmiMyRank();
  const int n = table->ranks;
  const int start = nextRand() % n;
  for(int t = 0; t < n; t++){
    const int r = (start + t) % n;
    if(r == me) continue;
    PeQueues &pq = table->pes[r];
    std::unique_lock<std::mutex> lk(pq.lock, std::try_to_lock);
    if(!lk.owns_lock()) continue;
    for(size_t i = 0; i < pq.pieces.size(); i++){
      TreePiece *tp = pq.pieces[i];
      std::deque<int> &q = tp->stealQueue();
      if(q.empty()) continue;
      const int g = q.front();
      if(!tp->groupQuiet(g)) continue;
      q.pop_front();
      if(q.empty()){
        pq.pieces[i] = pq.pieces.back();
        pq.pieces.pop_back();
      }
      table->queued.fetch_sub(1, std::memory_order_relaxed);
      lk.unlock();
      runStolen(tp, g);
      return;
    }
  }
}

} // namespace

void stealInit(){
  std::call_once(tableOnce, [](){
    Table *t = new Table;
    t->ranks = CmiMyNodeSize();
    t->pes = new PeQueues[t->ranks];
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

int stealPopOwn(TreePiece *tp, int maxBuckets, int *out, int cap, bool &more){
  PeQueues &pq = table->pes[CmiMyRank()];
  std::lock_guard<std::mutex> lk(pq.lock);
  std::deque<int> &q = tp->stealQueue();
  int k = 0, buckets = 0;
  while(k < cap && buckets < maxBuckets && !q.empty()){
    const int g = q.back();
    q.pop_back();
    buckets += tp->getGroup(g).numBuckets;
    out[k++] = g;
  }
  if(q.empty()){
    for(size_t i = 0; i < pq.pieces.size(); i++){
      if(pq.pieces[i] == tp){
        pq.pieces[i] = pq.pieces.back();
        pq.pieces.pop_back();
        break;
      }
    }
  }
  more = !q.empty();
  if(k > 0) table->queued.fetch_sub(k, std::memory_order_relaxed);
  return k;
}

#include "Traversal_defs.h"

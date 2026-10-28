#ifndef __STATE_H__
#define __STATE_H__

#include <iostream>
#include <fstream>
#include <sstream>
#include <set>
#include <map>
#include <atomic>
#include <memory>
#include "WalkGroup.h"

using namespace std;

extern string NodeTypeString[];

class TreePiece;
struct State {
  int pending;
  int current;
  Node<ForceData> **currentBucketPtr;
  TreePiece *ownerTreePiece;

#ifdef DEBUG_TRAVERSALS
  map<Key,set<Key> > bucketKeys;
  string description;
#endif

#ifdef STATISTICS
  CmiUInt8 numInteractions[3];
#endif

  // Per walk group of a remote traversal (Steal.h): requests in flight and
  // whether the owner's pass has walked it. A group is quiet when both say
  // the owner writes its particles no more, and only then may another PE do
  // its local walk. Written by the owner, read by thieves: atomics.
  const WalkGroup *groupBase;
  int numGroups, groupCap;
  std::unique_ptr<std::atomic<int>[]> groupOutstanding;
  std::unique_ptr<std::atomic<char>[]> groupWalked;

  void resetGroups(const WalkGroup *base, int n){
    groupBase = base;
    numGroups = n;
    if(n > groupCap){
      groupOutstanding.reset(new std::atomic<int>[n]);
      groupWalked.reset(new std::atomic<char>[n]);
      groupCap = n;
    }
    for(int g = 0; g < n; g++){
      groupOutstanding[g].store(0, std::memory_order_relaxed);
      groupWalked[g].store(0, std::memory_order_relaxed);
    }
  }
  int groupIndex(const WalkGroup *g) const { return (int)(g - groupBase); }
  void groupRequestIssued(int g){ groupOutstanding[g].fetch_add(1, std::memory_order_relaxed); }
  // After the resumed walk has flushed: release, so a thief that sees zero
  // also sees the particles it wrote.
  void groupRequestDone(int g){ groupOutstanding[g].fetch_sub(1, std::memory_order_release); }
  void groupWalkedByOwner(int g){ groupWalked[g].store(1, std::memory_order_release); }
  bool groupQuiet(int g) const {
    return groupWalked[g].load(std::memory_order_acquire) != 0 &&
           groupOutstanding[g].load(std::memory_order_acquire) == 0;
  }

  void incrPending() { pending++; }
  bool decrPending() {
    pending--;
    return complete();
  }

  bool decrPending(int n){
    pending -= n;
    return complete();
  }

  bool complete() { return (pending == 0); }

  State() : 
    pending(-1), 
    current(-1), 
    currentBucketPtr(NULL),
    ownerTreePiece(NULL),
    groupBase(NULL),
    numGroups(0),
    groupCap(0)
  {

#ifdef STATISTICS
    numInteractions[0] = 0;
    numInteractions[1] = 0;
    numInteractions[2] = 0;
#endif
  }
  
  void reset(TreePiece *owner, int p, Node<ForceData> **bucketPtr){
    ownerTreePiece = owner;
    pending = p;
    current = 0;
    currentBucketPtr = bucketPtr;
  }

  // Hooks the walk calls for every node it meets. They do nothing, and are
  // defined here so that the compiler can drop the calls: out of line, they
  // cost several calls per node visited.
  void nodeEncountered(Key bucketKey, Node<ForceData> *node) {}
  void nodeOpened(Key bucketKey, Node<ForceData> *node) {}
  void nodeDiscarded(Key bucketKey, Node<ForceData> *node) {}
  void nodeComputed(Node<ForceData> *bucket, Key nodeKey) {}
  void bucketComputed(Node<ForceData> *bucket, Key k) {}

  void finishedIteration(){
#ifdef STATISTICS
    numInteractions[0] = 0;
    numInteractions[1] = 0;
    numInteractions[2] = 0;
#endif
  }

  void insert(Key bucketKey, Key k){
  }

  void incrPartNodeInteractions(Key bucketKey, CmiUInt8 n){
#ifdef STATISTICS
    numInteractions[0] += n;
#endif
  }

  void incrPartPartInteractions(Key bucketKey, CmiUInt8 n){
#ifdef STATISTICS
    numInteractions[1] += n;
#endif
  }

  void incrOpenCriterion(int n = 1){
#ifdef STATISTICS
    numInteractions[2] += n;
#endif
  }
};

#endif

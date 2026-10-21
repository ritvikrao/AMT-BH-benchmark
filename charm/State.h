#ifndef __STATE_H__
#define __STATE_H__

#include <iostream>
#include <fstream>
#include <sstream>
#include <set>
#include <map>

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
    ownerTreePiece(NULL)
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

  void incrOpenCriterion(){
#ifdef STATISTICS
    numInteractions[2]++;
#endif
  }
};

#endif

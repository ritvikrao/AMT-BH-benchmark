#ifndef __TREE_PIECE_H__
#define __TREE_PIECE_H__

#include "defines.h"
#include <vector>
#include <deque>
#include "OrientedBox.h"
#include "barnes.decl.h"
#include "Particle.h"
#include "Messages.h"
#include "Node.h"
#include "Vector3D.h"
#include "State.h"

#include "Worker.h"

#include "MultipoleMoments.h"
#include "Traversal_decls.h"

class TreePiece : public CBase_TreePiece {
  int iteration;

  int myNumBuckets;
  Node<ForceData> **myBuckets;
  // This step's buckets that hold particles, and the walk groups cut from
  // them: consecutive runs (WalkGroup.h).
  std::vector<Node<ForceData>*> walkBuckets;
  std::vector<WalkGroup> walkGroups;
  void formWalkGroups();
  Node<ForceData> *root;
  // Local walks not yet started, by group index (Steal.h): the owner takes
  // from the back, idle PEs of the process from the front.
  std::deque<int> localQueue;

  State localTraversalState;
  State remoteTraversalState;

  LocalTraversalWorker localTraversalWorker;
  RemoteTraversalWorker remoteTraversalWorker;

  Traversal<ForceData> trav;

  DataManager *myDM;

  int localStateID;
  int remoteStateID;
  int totalNumTraversals;
  int numTraversalsDone;

  void traversalDone();
  void finishIteration();

  void checkTraversals();

  public:
  TreePiece();
  TreePiece(CkMigrateMessage *m);

  int getIndex() {return thisIndex;}

  void prepare(Node<ForceData> *_root, Node<ForceData> **buckets, int bucketStart, int bucketEnd);
  void startTraversal();

  void doLocalGravity(RescheduleMsg *);
  void doRemoteGravity(RescheduleMsg *);

  void requestParticles(RequestMsg *msg);
  void requestNode(RequestMsg *msg);

  void localGravityDone();
  void remoteGravityDone();
  // A PE of this process did group g's local walk (Steal.h): its sums for
  // the group's n particles, five per particle, and its interaction counts.
  void stolenLocalDone(int g, int n, double *sums, CmiUInt8 pn, CmiUInt8 pp, CmiUInt8 oc);

  // With -steal=1 the owner's own local walks are idle-time work too
  // (Steal.h): one group, on this PE's thread, outside any entry method.
  void doOwnLocalGroup(int g);
  // For stolen walks: the owner's tree and its groups.
  Node<ForceData> *getRoot() const { return root; }
  const WalkGroup &getGroup(int g) const { return walkGroups[g]; }
  std::deque<int> &stealQueue() { return localQueue; }
  void requestMoments(Key k, int replyTo);

  void quiescence();
  int getIteration();

  // Load balancing (-lbperiod): atSync() hands the element to the balancer
  // between steps, ResumeFromSync() reports where it landed.
  void atSync();
  void ResumeFromSync();

  // Work done for this TreePiece outside its own entry methods -- the walks
  // that resume in DataManager::recvNode/recvParticles when fetched data
  // arrives -- is charged to it between these two calls, so the balancer
  // sees it as this TreePiece's load rather than as the PE's background.
  void startLoad(){ getCkLocRec()->startTiming(1); }
  void stopLoad(){ getCkLocRec()->stopTiming(1); }

  void pup(PUP::er &p);
};

#endif

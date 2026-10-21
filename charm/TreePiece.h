#ifndef __TREE_PIECE_H__
#define __TREE_PIECE_H__

#include "defines.h"
#include <vector>
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
  // This step's walk groups: consecutive runs of myBuckets (WalkGroup.h).
  std::vector<WalkGroup> walkGroups;
  void formWalkGroups();
  Node<ForceData> *root;

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
  TreePiece(CkMigrateMessage *) {}

  int getIndex() {return thisIndex;}

  void prepare(Node<ForceData> *_root, Node<ForceData> **buckets, int bucketStart, int bucketEnd);
  void startTraversal();

  void doLocalGravity(RescheduleMsg *);
  void doRemoteGravity(RescheduleMsg *);

  void requestParticles(RequestMsg *msg);
  void requestNode(RequestMsg *msg);

  void localGravityDone();
  void remoteGravityDone();
  void requestMoments(Key k, int replyTo);

  void quiescence();
  int getIteration();

  void pup(PUP::er &p);
};

#endif

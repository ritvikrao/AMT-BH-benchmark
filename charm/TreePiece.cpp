#include "TreePiece.h"
#include "Messages.h"
#include "DataManager.h"
#include "Parameters.h"

#include <fstream>

extern CProxy_Main mainProxy;
extern CProxy_DataManager dataManagerProxy;
extern Parameters globalParams;

TreePiece::TreePiece() : 
  localTraversalState(),
  remoteTraversalState(),
  localStateID(0), 
  remoteStateID(1),
  localTraversalWorker(),
  remoteTraversalWorker(),
  totalNumTraversals(2),
  iteration(0),
  numTraversalsDone(0)
{
  myDM = dataManagerProxy.ckLocalBranch();
}

void TreePiece::prepare(Node<ForceData> *_root, Node<ForceData> **buckets, int bucketStart, int bucketEnd){
  root = _root;
  myBuckets = buckets+bucketStart;
  myNumBuckets = bucketEnd-bucketStart;
}

// Cut myBuckets into runs of at most WALK_GROUP_MAX_BUCKETS buckets and
// globalParams.walkGroupParticles particles (a bucket larger than that walks
// alone). Consecutive buckets are neighbours along the space-filling curve,
// so a group is compact and its buckets accept and open mostly the same
// nodes. -walkgroup=1 gives one bucket per group, the walk per bucket.
void TreePiece::formWalkGroups(){
  walkGroups.clear();
  int i = 0;
  while(i < myNumBuckets){
    WalkGroup g;
    g.buckets = myBuckets + i;
    g.numBuckets = 0;
    int particles = 0;
    while(i < myNumBuckets && g.numBuckets < WALK_GROUP_MAX_BUCKETS){
      int np = myBuckets[i]->getNumParticles();
      if(g.numBuckets > 0 && particles + np > globalParams.walkGroupParticles) break;
      particles += np;
      const Vector3D<Real> &cm = myBuckets[i]->data.moments.cm;
      g.cx[g.numBuckets] = cm.x;
      g.cy[g.numBuckets] = cm.y;
      g.cz[g.numBuckets] = cm.z;
      g.np[g.numBuckets] = np;
      g.numBuckets++;
      i++;
    }
    // visit() reads the centres four at a time; give the unused slots a
    // finite value (their bits are never in a mask).
    for(int b = g.numBuckets; b < WALK_GROUP_MAX_BUCKETS; b++){
      g.cx[b] = g.cy[b] = g.cz[b] = 0.0;
    }
    walkGroups.push_back(g);
  }
}

void TreePiece::startTraversal(){
  numTraversalsDone = 0;
  trav.setDataManager(myDM);

  if(myNumBuckets == 0){
    localGravityDone();
    remoteGravityDone();
    return;
  }

  formWalkGroups();
  const int numGroups = walkGroups.size();

  remoteTraversalState.reset(this,numGroups,NULL);
  remoteTraversalWorker.reset(this,&remoteTraversalState);
  RescheduleMsg *msg = new (NUM_PRIORITY_BITS) RescheduleMsg;
  *(int *)CkPriorityPtr(msg) = REMOTE_GRAVITY_PRIORITY;
  CkSetQueueing(msg, CK_QUEUEING_IFIFO);
  thisProxy[thisIndex].doRemoteGravity(msg);

  localTraversalState.reset(this,numGroups,NULL);
  localTraversalWorker.reset(this,&localTraversalState);

  msg = new (NUM_PRIORITY_BITS) RescheduleMsg;
  *(int *)CkPriorityPtr(msg) = LOCAL_GRAVITY_PRIORITY;
  CkSetQueueing(msg, CK_QUEUEING_IFIFO);
  thisProxy[thisIndex].doLocalGravity(msg);
}

// Walks groups until yieldPeriod buckets' worth are done, then yields so that
// fetch replies and requests from other PEs are served in between.
void TreePiece::doLocalGravity(RescheduleMsg *msg){
  PE_DIAG_TIME(busyLocal);
  const int numGroups = walkGroups.size();
  int i = 0, buckets = 0;
  while(buckets < globalParams.yieldPeriod && localTraversalState.current < numGroups){
    const WalkGroup &g = walkGroups[localTraversalState.current];
    localTraversalWorker.setContext(WalkContext(&g,allBuckets(g)));
    trav.gravityWalk(root,&localTraversalWorker,&localTraversalState,allBuckets(g));
    flushAllSources();
    localTraversalState.current++;
    buckets += g.numBuckets;
    i++;
  }

  if(localTraversalState.decrPending(i)){
    localGravityDone();
    delete msg;
  }
  else if(localTraversalState.current < numGroups) {
    thisProxy[thisIndex].doLocalGravity(msg);
  }
}

void TreePiece::doRemoteGravity(RescheduleMsg *msg){
  PE_DIAG_TIME(busyRemote);
  const int numGroups = walkGroups.size();
  int i = 0, buckets = 0;
  while(buckets < globalParams.yieldPeriod && remoteTraversalState.current < numGroups){
    const WalkGroup &g = walkGroups[remoteTraversalState.current];
    remoteTraversalWorker.setContext(WalkContext(&g,allBuckets(g)));
    trav.gravityWalk(root,&remoteTraversalWorker,&remoteTraversalState,allBuckets(g));
    flushAllSources();
    remoteTraversalState.current++;
    buckets += g.numBuckets;
    i++;
  }

  if(remoteTraversalState.decrPending(i)){
    remoteGravityDone();
    delete msg;
  }
  else if (remoteTraversalState.current < numGroups) {
    thisProxy[thisIndex].doRemoteGravity(msg);
  }
}

void TreePiece::localGravityDone(){
  traversalDone();
}

void TreePiece::remoteGravityDone(){ 
  traversalDone();
}

void TreePiece::traversalDone(){ 
  numTraversalsDone++;

  if(numTraversalsDone == totalNumTraversals){
#ifdef STATISTICS
    CmiUInt8 pn = localTraversalState.numInteractions[0]+remoteTraversalState.numInteractions[0];
    CmiUInt8 pp = localTraversalState.numInteractions[1]+remoteTraversalState.numInteractions[1];
    CmiUInt8 oc = localTraversalState.numInteractions[2]+remoteTraversalState.numInteractions[2];
    dataManagerProxy[CkMyPe()].traversalsDone(pn,pp,oc);
#else
    dataManagerProxy[CkMyPe()].traversalsDone();
#endif

    finishIteration();
  }
}

void TreePiece::finishIteration(){
  numTraversalsDone = 0;

  localTraversalState.finishedIteration();
  remoteTraversalState.finishedIteration();


  iteration++;

  checkTraversals();
}

void TreePiece::checkTraversals(){
}

void TreePiece::quiescence(){
  CkPrintf("QUIESCENCE tree piece %d proc %d numBuckets %d trav_done %d outstanding local %d remote %d\n", 
                thisIndex,
                CkMyPe(),
                myNumBuckets,
                numTraversalsDone,
                localTraversalState.pending,
                remoteTraversalState.pending);

  CkCallback cb(CkIndex_Main::quiescenceExit(),mainProxy);
  contribute(0,0,CkReduction::sum_int,cb);
}

void TreePiece::requestMoments(Key k, int replyTo){
  myDM->requestMoments(k,replyTo);
}

void TreePiece::requestParticles(RequestMsg *msg){
  // forward the request to the DM, since TPs don't
  // really own particles or nodes
  myDM->requestParticles(msg);
}

void TreePiece::requestNode(RequestMsg *msg){
  myDM->requestNode(msg);
}

int TreePiece::getIteration() {
  return iteration;
}

void TreePiece::pup(PUP::er &p){
  p | iteration;
  if(p.isUnpacking()){
  }
}

#include "Traversal_defs.h"


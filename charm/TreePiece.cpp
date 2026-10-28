#include "TreePiece.h"
#include "Messages.h"
#include "DataManager.h"
#include "Parameters.h"
#include "Steal.h"

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
  usesAtSync = true;
}

// A migrated TreePiece starts exactly like a new one; pup() then restores
// the step count.
TreePiece::TreePiece(CkMigrateMessage *m) :
  CBase_TreePiece(m),
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
  usesAtSync = true;
}

void TreePiece::prepare(Node<ForceData> *_root, Node<ForceData> **buckets, int bucketStart, int bucketEnd){
  root = _root;
  myBuckets = buckets+bucketStart;
  myNumBuckets = bucketEnd-bucketStart;
}

// Cut the buckets that hold particles into runs of at most
// WALK_GROUP_MAX_BUCKETS buckets and globalParams.walkGroupParticles
// particles (a bucket larger than that walks alone). Consecutive buckets are
// neighbours along the space-filling curve, so a group is compact and its
// buckets accept and open mostly the same nodes. -walkgroup=1 gives one
// bucket per group, the walk per bucket. Empty buckets are left out: they
// have no forces to compute, and a refinement leaves many of them, most of
// all in an octree.
void TreePiece::formWalkGroups(){
  walkBuckets.clear();
  for(int i = 0; i < myNumBuckets; i++)
    if(myBuckets[i]->getNumParticles() > 0) walkBuckets.push_back(myBuckets[i]);
  const int numWalkBuckets = walkBuckets.size();

  walkGroups.clear();
  int i = 0;
  while(i < numWalkBuckets){
    WalkGroup g;
    g.buckets = walkBuckets.data() + i;
    g.numBuckets = 0;
    int particles = 0;
    while(i < numWalkBuckets && g.numBuckets < WALK_GROUP_MAX_BUCKETS){
      int np = walkBuckets[i]->getNumParticles();
      if(g.numBuckets > 0 && particles + np > globalParams.walkGroupParticles) break;
      particles += np;
      const Vector3D<Real> &cm = walkBuckets[i]->data.moments.cm;
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
    g.packet = globalParams.packetWalk && particles <= PACKET_MAX_LANES;
    walkGroups.push_back(g);
  }
}

void TreePiece::startTraversal(){
  numTraversalsDone = 0;
  trav.setDataManager(myDM);

  formWalkGroups();
  const int numGroups = walkGroups.size();
  if(numGroups == 0){
    localGravityDone();
    remoteGravityDone();
    return;
  }

  remoteTraversalState.reset(this,numGroups,NULL);
  remoteTraversalWorker.reset(this,&remoteTraversalState);
  RescheduleMsg *msg = new (NUM_PRIORITY_BITS) RescheduleMsg;
  *(int *)CkPriorityPtr(msg) = REMOTE_GRAVITY_PRIORITY;
  CkSetQueueing(msg, CK_QUEUEING_IFIFO);
  thisProxy[thisIndex].doRemoteGravity(msg);

  localTraversalState.reset(this,numGroups,NULL);
  localTraversalWorker.reset(this,&localTraversalState);
  if(globalParams.steal){
    stealRemoteOpened();
    // The local walks are done from the idle callback -- by this PE when it
    // has nothing else to do, by another PE of the process otherwise -- so
    // no doLocalGravity message: the remote walk alone drives the critical
    // path and the local half fills the gaps on whichever PE has them.
    stealPublish(this,numGroups);
    return;
  }

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
    localTraversalWorker.endWalk();
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
    remoteTraversalWorker.endWalk();
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

void TreePiece::doOwnLocalGroup(int g){
  PE_DIAG_TIME(busyLocal);
  const WalkGroup &grp = walkGroups[g];
  localTraversalWorker.setContext(WalkContext(&grp,allBuckets(grp)));
  trav.gravityWalk(root,&localTraversalWorker,&localTraversalState,allBuckets(grp));
  localTraversalWorker.endWalk();
  if(localTraversalState.decrPending(1)) localGravityDone();
}

// Add what another PE's walk of group g gathered into the group's particles:
// this PE is the only writer of its particles, so no lock, whatever the
// state of the group's remote walk.
void TreePiece::stolenLocalDone(int g, int n, double *sums, CmiUInt8 pn, CmiUInt8 pp, CmiUInt8 oc){
  const WalkGroup &grp = walkGroups[g];
  int k = 0;
  for(int b = 0; b < grp.numBuckets; b++){
    Particle *p = grp.buckets[b]->getParticles();
    const int np = grp.buckets[b]->getNumParticles();
    for(int i = 0; i < np; i++, k++){
      const double *s = sums + 5*k;
      p[i].acceleration.x += s[0];
      p[i].acceleration.y += s[1];
      p[i].acceleration.z += s[2];
      p[i].potential += s[3];
      p[i].cost += s[4];
    }
  }
  CkAssert(k == n);
#ifdef STATISTICS
  localTraversalState.numInteractions[0] += pn;
  localTraversalState.numInteractions[1] += pp;
  localTraversalState.numInteractions[2] += oc;
#endif
  stealStats.lost++;
  if(localTraversalState.decrPending(1)) localGravityDone();
}

void TreePiece::remoteGravityDone(){ 
  if(globalParams.steal && !walkGroups.empty()) stealRemoteClosed();
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

// A TreePiece carries nothing between steps but its step count: its key
// range, particles, buckets and walk state are all rebuilt by the next
// step's decomposition, on whichever PE it then lives. So migrating one
// moves no particles -- the next exchange sends them to the new PE.
void TreePiece::pup(PUP::er &p){
  p | iteration;
  if(p.isUnpacking()){
    usesAtSync = true;
    myDM = dataManagerProxy.ckLocalBranch();
  }
}

// Called by DataManager::advance once every traversal of the step is over:
// until then a TreePiece answers other PEs' requests from this PE's tree, and
// must not move.
void TreePiece::atSync(){
  AtSync();
}

// Every DataManager needs every TreePiece's location before the next exchange
// (DataManager::homeOf). Each element fills its own slot with its PE + 1.
void TreePiece::ResumeFromSync(){
  const int n = globalParams.numTreePieces;
  std::vector<int> where(n, 0);
  where[thisIndex] = CkMyPe() + 1;
  CkCallback cb(CkIndex_DataManager::lbDone(NULL), dataManagerProxy);
  contribute(n*sizeof(int), where.data(), CkReduction::sum_int, cb);
}

#include "Traversal_defs.h"


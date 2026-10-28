#ifndef __DATA_MANAGER_H__
#define __DATA_MANAGER_H__

#include "Particle.h"

#include "OrientedBox.h"
#include "barnes.decl.h"
#include "Node.h"
#include "Descriptor.h"
#include "ActiveBinInfo.h"

#include "Traversal_decls.h"
#include "Request.h"
#include "Timers.h"

class TreePiece;

#include <map>
#include <vector>
using namespace std;

class TreePieceCounter : public CkLocIterator {            
  public:
  int count;
  CkHashtableT<CkArrayIndex, int> registered;               
  TreePieceCounter() : count(0) { }                      
  void addLocation(CkLocation &loc) {
    registered.put(loc.getIndex()) = ++count;               
  }
  void reset() {                                            
    count = 0;
    registered.empty();                                     
  }                                                         
};

struct RequestedMomentsDescriptor {
  Node<ForceData> *node;
  int numOutstanding;

  RequestedMomentsDescriptor() : 
    node(NULL), numOutstanding(-1)
  {
  }

  RequestedMomentsDescriptor(Node<ForceData> *nd, int n) : 
    node(nd), numOutstanding(n)
  {
  }

};

struct CacheStats {
  int outstandingRequests;
  int outstandingDeliveries;

  CacheStats() : 
    outstandingRequests(0),
    outstandingDeliveries(0)
  {
  }

  void incrRequests(){ outstandingRequests++; }
  void decrRequests(int n=1){ outstandingRequests -= n; }
  void incrDeliveries(){ outstandingDeliveries++; }
  void decrDeliveries(int n=1){ outstandingDeliveries -= n; }
  bool test(){ return (outstandingRequests==0) && (outstandingDeliveries==0); }
};

class DataManager : public CBase_DataManager {
  int numRankBits;
  double prevIterationStart;
  double avgIterationRuntime;


  CkVec<Particle> myParticles;
  int myNumParticles;

  // CSV input only: the reader runs in two phases, so the caller's completion
  // callback and the per-byte-range record counts have to outlive phase 1.
  CkCallback loadParticlesCb;
  int *csvRangeCounts;

  // Per-phase wall clock for this PE. Reduced once, after the final step.
  PhaseTimers timers;

  // ParaView output: the steps snapshotted so far. PE 0 alone keeps this, to
  // rewrite the .pvd collection after each snapshot.
  std::vector<int> writtenSteps;

  bool firstSplitterRound;

  Node<NodeDescriptor> *sortingRoot;
  int numTreePieces;

  // -reuseleaves: the keys of last step's histogram leaves, in TreePiece
  // order. Every PE has them, because every PE applied the same splits.
  std::vector<Key> lastLeaves;
  // This step's decomposition started from lastLeaves; and, on PE 0, how
  // many leaf families it merged and bins it split to get back in tolerance.
  bool reusingLeaves;
  int familiesMerged;
  int binsSplit;
  // Smallest and largest particle count over the TreePieces. Each histogram
  // leaf becomes one TreePiece, so these are the extremes of the final
  // histogram, known exactly on PE 0 and nowhere else. Reported once, in the
  // balance line.
  int tpMinParticles;
  int tpMaxParticles;

  int iteration;
  int decompIterations;
  // This PE's particles' interactions last step (Steal.h, heavy PEs).
  double myInteractionsLast;
  // Warn once, not once per decomposition round, when -p runs out.
  bool warnedTreePieceBudget;
  ActiveBinInfo<NodeDescriptor> activeBins;
  // This PE's sparse histogram contribution (sendHistogram) and, on PE 0,
  // the merged histogram expanded to one descriptor per bin
  // (receiveHistogram). Members so their storage is reused across rounds.
  std::vector<SparseBin> histogramContribution;
  std::vector<NodeDescriptor> histogramDense;

  TreePieceCounter localTreePieces;
  int numLocalTreePieces;
  CkVec<TreePieceDescriptor> submittedParticles;
  Node<ForceData> *root;

  Key *keyRanges;
  // Exact particle count per TreePiece from the final histogram round: owned
  // by PE 0, the process's shared RangeMsg (RangeShare) elsewhere.
  int *tpCounts;
  bool haveRanges;

  // The particle exchange. Each DM sends one message per destination PE that
  // gets any of its particles (not one per TreePiece), and a DM has all of its
  // particles once it holds exactly the number the counts promise its local
  // TreePieces -- no empty messages, no per-TreePiece barrier.
  CkVec<ParticleMsg *> arrivedParticleMsgs;
  long numParticlesArrived;
  long numParticlesExpected;
  // PE of every TreePiece, so that a sender can address the DM rather than
  // the TreePiece: the home PE until a load balancing step, then wherever
  // the balancer put it (lbDone).
  std::vector<int> tpHome;
  // Load balancing between steps (-lbperiod). The bounding box that starts
  // the next step waits here until both this PE's advance() and the new
  // TreePiece map (lbDone) are in, which can arrive in either order.
  BoundingBox lbBox;
  bool lbBoxReady;
  bool lbMapReady;
  double lbStart;
  void resumeAfterLB();
  // Flush state: runs of particles bound for flushDest, sent as one message.
  int flushDest;
  std::vector<std::pair<Particle *, int> > flushRuns;
  // Runs bound for this PE itself: left in myParticles, never put in a
  // message. They lie in key order and in address order, so after the flush
  // they are compacted in place and the arrivals merged in.
  std::vector<std::pair<Particle *, int> > keptRuns;
  long numParticlesKept;
  void emitFlushRun();
  int homeOf(int tp);
  void expectParticles();
  void tryProcessParticles();
  void buildLocalDescriptors();
  CkVec<Node<ForceData>*> myBuckets;

  // I am done constructing the tree 
  // from particles present on this PE
  bool doneTreeBuild;
  //CkVec<RequestedMomentsDescriptor> requestedMoments;

  map<Key,CkVec<int> > pendingMoments;

  // Remote leaves with no particles anywhere, found by the moments walk and
  // settled locally instead of being asked for; see settleEmptyRemoteLeaves.
  CkVec<Node<ForceData>*> emptyRemoteLeaves;

  // I have processed the moment 
  // contributions from all other PEs, so that
  // the tree on this PE is now ready for 
  // traversal
  bool treeMomentsReady;
  CkVec<RequestMsg *> bufferedNodeRequests;
  CkVec<RequestMsg *> bufferedParticleRequests;

  Traversal<NodeDescriptor> scaffoldTrav;
  Traversal<ForceData> fillTrav;

  map<Key,Request> nodeRequestTable;
  map<Key,Request> particleRequestTable;

  int numTreePiecesDoneTraversals;
  CacheStats nodeReqs;
  CacheStats partReqs;


#ifdef STATISTICS
  CmiUInt8 numInteractions[3];
#endif

#ifdef CHECK_TRAVERSAL_MASS
  void checkTraversalMass();
#endif

  void kickDriftKick(OrientedBox<Real> &box, Real &kineticEnergy, Real &potentialEnergy);

  void writeSnapshot();
  void dumpForces();

  void loadParticlesBinary(const CkCallback &cb);
  void loadParticlesCsv(const CkCallback &cb);

  void hashParticleCoordinates(const OrientedBox<Real> &universe);
  void initHistogramParticles();
  void refineToLeaves(Node<NodeDescriptor> *node, int lo, int hi);
  void sendHistogram();
  
  void senseTreePieces();
  void buildTree();

  void reportBalance();

  void printTree();
  void flushParticles();

  void processSubmittedParticles();
  void makeMoments();
  void settleEmptyRemoteLeaves();
  void flushMomentRequests();
  void respondToMomentsRequest(Node<ForceData> *,CkVec<int>&);
  Node<ForceData> *lookupNode(Key k);

  void updateLeafMoments(Node<ForceData> *node, MomentsExchangeStruct &data);
  void passMomentsUpward(Node<ForceData> *node);
  void treeReady();

  void startTraversal();
  void flushBufferedRemoteDataRequests();

  void freeCachedData();
  void freeTree();
  void finishIteration();

  void findMinVByA(DtReductionStruct &);
#ifdef PE_DIAG
  void reportPeDiag();
#endif

  void markNaNBuckets();

  public:
  DataManager();

  void loadParticles(const CkCallback &cb);
  void receiveCsvCounts(CkReductionMsg *msg);

  void decompose(const BoundingBox &universe);
  void receiveHistogram(CkReductionMsg *msg);
  void receiveSplitters(SplitterMsg *msg);
  void rangesReady();
  void sendParticlesToTreePiece(Node<NodeDescriptor> *nd, int tp);

  void receiveMoments(MomentsMsg *msg);
  
  // called by tree pieces
  void receiveParticles(ParticleMsg *msg);
  void requestMoments(Key k, int replyTo);
  void advance(CkReductionMsg *);
  // Steal.h: a local walk of another PE's TreePiece, and one of this PE's own,
  // run from the idle callback through an inline entry method.
  void stolenWalk(CmiUInt8 treePiece, int g);
  void ownIdleWalk(CmiUInt8 treePiece, int g);
#ifdef STATISTICS
  void traversalsDone(CmiUInt8 pnInter, CmiUInt8 ppInter, CmiUInt8 openCrit);
#else
  void traversalsDone();
#endif

  // called by tree piece that is making a request
  void requestNode(Node<ForceData> *leaf, TraversalWorker *worker, State *state, Traversal<ForceData> *callbackTraversal, const WalkContext &context);
  void requestParticles(Node<ForceData> *leaf, TraversalWorker *worker, State *state, Traversal<ForceData> *callbackTraversal, const WalkContext &context);

  // called by tree piece that is forwarding a remote request
  void requestNode(RequestMsg *msg);
  void requestParticles(RequestMsg *msg);
  
  void recvParticles(ParticleReplyMsg *msg);
  void recvNode(NodeReplyMsg *msg);

  void recvUnivBoundingBox(CkReductionMsg *msg);
  void lbDone(CkReductionMsg *msg);

  void quiescence();

  void addBucketNodeInteractions(Key k, CmiUInt8 pn);
  void addBucketPartInteractions(Key k, CmiUInt8 pp);
};

#endif

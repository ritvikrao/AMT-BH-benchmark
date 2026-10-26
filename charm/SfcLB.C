#include "SfcLB.h"
#include "barnes.decl.h"
#include "objid.h"

#include <algorithm>
#include <vector>

extern CProxy_TreePiece treePieceProxy;

static void lbinit(){
  LBRegisterBalancer<SfcLB>("SfcLB",
    "contiguous runs of TreePieces along the space-filling curve, by measured load");
}

SfcLB::SfcLB(const CkLBOptions &opt) : CBase_SfcLB(opt){
  init();
}

void SfcLB::init(){
  lbname = "SfcLB";
}

void SfcLB::work(BaseLB::LDStats *stats){
  const int numObjs = stats->objData.size();
  const int nProcs = stats->nprocs();
  CkLocMgr *mgr = treePieceProxy.ckLocMgr();

  for(int i = 0; i < numObjs; i++) stats->to_proc[i] = stats->from_proc[i];

  // Background load (the DataManager's work: decomposition, exchange, tree
  // build, and the walks that resume when fetched data arrives) is not any
  // one object's, so like ChaNGa give every piece an equal share of it.
  double bg = 0.0;
  for(int p = 0; p < nProcs; p++) bg += stats->procs[p].bg_walltime;
  const double bgPerPiece = (numObjs > 0) ? bg/numObjs : 0.0;

  std::vector<Piece> pieces;
  pieces.reserve(numObjs);
  double totalLoad = 0.0;
  for(int i = 0; i < numObjs; i++){
    LDObjData &od = stats->objData[i];
    if(!od.migratable) continue;
    // The balancer's object id is the element's id; the TreePiece array has
    // bounds, so the location manager decodes the index from it directly.
    const CmiUInt8 eid = ck::ObjID(od.objID()).getElementID();
    Piece pc;
    pc.lbindex = i;
    pc.tp = mgr->lookupIdx(eid).data()[0];
    pc.load = od.wallTime + bgPerPiece;
    totalLoad += pc.load;
    pieces.push_back(pc);
  }
  std::sort(pieces.begin(), pieces.end());
  for(size_t k = 1; k < pieces.size(); k++){
    if(pieces[k].tp == pieces[k-1].tp)
      CkAbort("SfcLB: two objects map to TreePiece %d\n", pieces[k].tp);
  }

  partition(nProcs, pieces, totalLoad, stats);
  report(pieces, bgPerPiece, stats);
}

// MultistepLB_SFC::sfcPartition: walk the curve, giving each PE pieces while
// adding the next one brings the cumulative load closer to the PE's share of
// the total, and always at least one.
void SfcLB::partition(int nProcs, const std::vector<Piece> &pieces,
                      double totalLoad, BaseLB::LDStats *stats){
  const int nPieces = pieces.size();
  double loadPrev = 0.0;   // load on all lower PEs
  int cur = 0;
  for(int p = 0; p < nProcs && cur < nPieces; p++){
    if(!stats->procs[p].available) continue;
    stats->to_proc[pieces[cur].lbindex] = p;
    double loadCurr = pieces[cur].load;
    cur++;
    const double target = (p+1)*totalLoad/nProcs;
    double err = fabs(loadPrev + loadCurr - target);
    while(cur < nPieces && fabs(pieces[cur].load + loadPrev + loadCurr - target) < err){
      stats->to_proc[pieces[cur].lbindex] = p;
      loadCurr += pieces[cur].load;
      err = fabs(loadPrev + loadCurr - target);
      cur++;
    }
    loadPrev += loadCurr;
  }
  // Rounding can leave the last few pieces after the last PE's target.
  for(; cur < nPieces; cur++) stats->to_proc[pieces[cur].lbindex] = nProcs-1;
}

// Measured load per PE before, and predicted after.
void SfcLB::report(const std::vector<Piece> &pieces, double bgPerPiece,
                   BaseLB::LDStats *stats){
  const int nProcs = stats->nprocs();
  std::vector<double> before(nProcs, 0.0), after(nProcs, 0.0);
  int moved = 0;
  for(const Piece &pc : pieces){
    before[stats->from_proc[pc.lbindex]] += pc.load;
    after[stats->to_proc[pc.lbindex]] += pc.load;
    if(stats->to_proc[pc.lbindex] != stats->from_proc[pc.lbindex]) moved++;
  }
  double sum = 0.0;
  for(int p = 0; p < nProcs; p++) sum += before[p];
  const double mean = sum/nProcs;
  CkPrintf("[SfcLB] %zu pieces, background %.3g s per piece; load max/mean %.3f "
           "measured -> %.3f predicted; %d pieces move\n",
           pieces.size(), bgPerPiece,
           *std::max_element(before.begin(), before.end())/mean,
           *std::max_element(after.begin(), after.end())/mean, moved);
}

#include "SfcLB.def.h"

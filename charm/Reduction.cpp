#include "charm++.h"
#include "OrientedBox.h"
#include "defines.h"
#include "ActiveBinInfo.h"
#include <vector>

CkReduction::reducerType BoundingBoxGrowReductionType;
CkReduction::reducerType NodeDescriptorReductionType;
CkReduction::reducerType DtReductionType;
CkReduction::reducerType PhaseTimerReductionType;

CkReductionMsg *BoundingBoxGrowReduction(int nmsgs, CkReductionMsg **msgs){
  BoundingBox &bb = *((BoundingBox *) msgs[0]->getData());
  for(int i = 1; i < nmsgs; i++){
    CkAssert(msgs[i]->getSize() == sizeof(BoundingBox));
    BoundingBox &other = *((BoundingBox *) msgs[i]->getData());
    bb.grow(other);
  }
  return CkReductionMsg::buildNew(sizeof(BoundingBox),&bb);
}

// Merge sparse histogram contributions (SparseBin lists, each in bin order,
// possibly empty). Bins are combined with NodeDescriptor::grow -- a sum of
// counts and costs and a min/max of keys -- so the result does not depend on
// the order the messages arrive in. A dense scratch array over the bins seen
// keeps the merge linear in the entries plus the largest bin index; the
// output lists only the bins that have particles, in bin order.
CkReductionMsg *NodeDescriptorReduction(int nmsgs, CkReductionMsg **msgs){
  int maxBin = -1;
  for(int j = 0; j < nmsgs; j++){
    const int n = msgs[j]->getSize()/sizeof(SparseBin);
    const SparseBin *e = (const SparseBin *)msgs[j]->getData();
    if(n > 0 && e[n-1].bin > maxBin) maxBin = e[n-1].bin;
  }
  if(maxBin < 0) return CkReductionMsg::buildNew(0, NULL);

  std::vector<NodeDescriptor> acc(maxBin+1);
  std::vector<char> seen(maxBin+1, 0);
  int numOut = 0;
  for(int j = 0; j < nmsgs; j++){
    const int n = msgs[j]->getSize()/sizeof(SparseBin);
    const SparseBin *e = (const SparseBin *)msgs[j]->getData();
    for(int i = 0; i < n; i++){
      const int b = e[i].bin;
      if(!seen[b]){ seen[b] = 1; numOut++; acc[b] = e[i].d; }
      else acc[b].grow(e[i].d);
    }
  }

  CkReductionMsg *out = CkReductionMsg::buildNew(sizeof(SparseBin)*numOut, NULL);
  SparseBin *o = (SparseBin *)out->getData();
  int k = 0;
  for(int b = 0; b <= maxBin; b++){
    if(!seen[b]) continue;
    o[k].bin = b;
    o[k].d = acc[b];
    k++;
  }
  CkAssert(k == numOut);
  return out;
}

CkReductionMsg *DtReduction(int nmsgs, CkReductionMsg **msgs){
  int numElements = (msgs[0]->getSize()/sizeof(DtReductionStruct));

  for(int i = 0; i < numElements; i++){
    DtReductionStruct *zeroth = ((DtReductionStruct*)(msgs[0]->getData()))+i;
    for(int j = 1; j < nmsgs; j++){
      const DtReductionStruct *source = ((const DtReductionStruct *)(msgs[j]->getData()))+i;
      *zeroth += *source;
    }
  }
  return CkReductionMsg::buildNew(msgs[0]->getSize(),msgs[0]->getData());
}

// Phase timings from every PE, as 2n doubles: the first n reduce by maximum
// (the slowest PE, which is what sets the critical path) and the second n by
// sum (divided by the PE count later, to show how far the mean sits below it).
// One reducer rather than two reductions, so the end of a run costs one
// message per PE.
CkReductionMsg *PhaseTimerReduction(int nmsgs, CkReductionMsg **msgs){
  const int total = (int)(msgs[0]->getSize()/sizeof(double));
  const int n = total/2;
  double *acc = (double *)msgs[0]->getData();

  for(int j = 1; j < nmsgs; j++){
    CkAssert(msgs[j]->getSize() == msgs[0]->getSize());
    const double *source = (const double *)msgs[j]->getData();
    for(int i = 0; i < n; i++){
      if(source[i] > acc[i]) acc[i] = source[i];
      acc[n+i] += source[n+i];
    }
  }
  return CkReductionMsg::buildNew(msgs[0]->getSize(),acc);
}

void registerReducers(){
  PhaseTimerReductionType = CkReduction::addReducer(PhaseTimerReduction);
  BoundingBoxGrowReductionType = CkReduction::addReducer(BoundingBoxGrowReduction);
  NodeDescriptorReductionType = CkReduction::addReducer(NodeDescriptorReduction);
  DtReductionType = CkReduction::addReducer(DtReduction);
}

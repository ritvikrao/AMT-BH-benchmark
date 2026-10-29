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

// Merge SparseBin lists, each in bin order and possibly empty, as a k-way
// merge of sorted lists: a cursor per input, the smallest bin next, equal
// bins combined with NodeDescriptor::grow (order-independent). The cost is
// proportional to the entries, not to the number of bins: the bins are the
// TreePieces, which at scale run to hundreds of thousands, while a PE's list
// has a few dozen entries and a process root's a few hundred.
CkReductionMsg *NodeDescriptorReduction(int nmsgs, CkReductionMsg **msgs){
  std::vector<const SparseBin *> cur(nmsgs), end(nmsgs);
  size_t total = 0;
  for(int j = 0; j < nmsgs; j++){
    const int n = msgs[j]->getSize()/sizeof(SparseBin);
    cur[j] = (const SparseBin *)msgs[j]->getData();
    end[j] = cur[j] + n;
    total += n;
  }
  std::vector<SparseBin> out;
  out.reserve(total);
  while(true){
    int b = -1;
    for(int j = 0; j < nmsgs; j++){
      if(cur[j] != end[j] && (b < 0 || cur[j]->bin < b)) b = cur[j]->bin;
    }
    if(b < 0) break;
    SparseBin merged;
    merged.bin = b;
    bool first = true;
    for(int j = 0; j < nmsgs; j++){
      if(cur[j] != end[j] && cur[j]->bin == b){
        if(first){ merged.d = cur[j]->d; first = false; }
        else merged.d.grow(cur[j]->d);
        cur[j]++;
        CkAssert(cur[j] == end[j] || cur[j]->bin > b);
      }
    }
    out.push_back(merged);
  }
  return CkReductionMsg::buildNew(sizeof(SparseBin)*out.size(),
                                  out.empty() ? NULL : out.data());
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

#ifndef __RANGE_SHARE_H__
#define __RANGE_SHARE_H__

#include "charm++.h"
#include "barnes.decl.h"
#include "Messages.h"

extern CProxy_DataManager dataManagerProxy;

// Holds the decomposition's result -- the RangeMsg with every TreePiece's key
// range and particle count -- once per process, and tells each PE of the
// process that it is there (DataManager::rangesReady). PE 0 used to broadcast
// the message to the DataManager group, and inside a process that broadcast
// is delivered one PE after another, each getting its own copy: 80 KB at
// -p 4016, about 21 us per PE on Delta, so the last of 60 PEs saw the
// ranges 1.2 ms after the first (and the last of 120, 2.5 ms), with every
// PE idle until then. A nodegroup broadcast makes one copy per process and
// the notifications are empty messages.
//
// The message is kept until the next step's ranges arrive. By then every PE
// of the process has passed advance(), where the DataManagers drop their
// pointers into it: the next decomposition cannot start before that.
class RangeShare : public CBase_RangeShare {
  RangeMsg *msg;

public:
  RangeShare() : msg(NULL) {}
  ~RangeShare(){ delete msg; }

  void receiveRanges(RangeMsg *m){
    delete msg;
    msg = m;
    const int first = CkNodeFirst(CkMyNode());
    const int n = CkNodeSize(CkMyNode());
    for(int pe = first; pe < first+n; pe++) dataManagerProxy[pe].rangesReady();
  }

  const RangeMsg *ranges() const { return msg; }
};

#endif

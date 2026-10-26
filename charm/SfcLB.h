#ifndef __SFC_LB_H__
#define __SFC_LB_H__

#include "CentralLB.h"
#include "SfcLB.decl.h"

// Load balancing along the space-filling curve (+balancer SfcLB), ported
// from ChaNGa's MultistepLB_SFC (sfcPartition).
//
// The TreePieces are laid out in curve order and cut into one contiguous run
// per PE, each run's load as close as it can get to the PE's share. A PE so
// keeps one compact region of space: what its tree is built from, what its
// buckets' walks share, and what other PEs fetch from it. The generic
// balancers (GreedyRefineLB, ...) place each TreePiece by its own measured
// load alone, scatter a PE's TreePieces over the whole volume, and make the
// traversal several times slower than no balancing at all (MULTINODE.md,
// "Load balancing").
//
// ChaNGa sorts TreePieces by the curve key of their centroids, passed in as
// LB user data. Ours need no centroid: the decomposition hands key ranges out
// in TreePiece index order, so index order is curve order, and the index comes
// back from the object id. So neither user data nor a Charm++ built with
// --enable-lbuserdata is needed.
class SfcLB : public CBase_SfcLB {
  void init();
  bool QueryBalanceNow(int step) { return true; }

  struct Piece {
    int lbindex;   // into stats->objData
    int tp;        // TreePiece index: its place on the curve
    double load;
    bool operator<(const Piece &o) const { return tp < o.tp; }
  };

  void partition(int nProcs, const std::vector<Piece> &pieces,
                 double totalLoad, BaseLB::LDStats *stats);
  void report(const std::vector<Piece> &pieces, double bgPerPiece,
              BaseLB::LDStats *stats);

  public:
  SfcLB(const CkLBOptions &opt);
  SfcLB(CkMigrateMessage *m) : CBase_SfcLB(m) { init(); }

  void work(BaseLB::LDStats *stats);
};

#endif

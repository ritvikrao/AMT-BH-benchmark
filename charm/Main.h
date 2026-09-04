#ifndef __MAIN_H__
#define __MAIN_H__

#include "Parameters.h"
#include "defines.h"
#include "OrientedBox.h"

#include "barnes.decl.h"

class Main : public CBase_Main {
  Parameters params;
  int numQuiescenceRecvd;

  void getNumParticles();
  void scanBinaryInput();
  void scanCsvInput();
  void setParameters(CkArgMsg *m);
  void usage();

  public:
  Main(CkArgMsg *msg);
  void commence();
  void niceExit();
  void reportFinalEnergy(CkReductionMsg *msg);

  void quiescence();
  void quiescenceExit();
};

#endif

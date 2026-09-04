#include "Main.h"
#include "Parameters.h"

#include "TreePiece.h"
#include "DataManager.h"
#include "Reduction.h"
#include "Messages.h"

#include "defaults.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <limits.h>
using namespace std;

CProxy_TreePiece treePieceProxy;
CProxy_DataManager dataManagerProxy;
CProxy_Main mainProxy;

Parameters globalParams;

Main::Main(CkArgMsg *msg){
  setParameters(msg);

  dataManagerProxy = CProxy_DataManager::ckNew();

  CkArrayOptions opts(globalParams.numTreePieces);
  treePieceProxy = CProxy_TreePiece::ckNew(opts);

  mainProxy = thisProxy;

  thisProxy.commence();

  CkCallback cb(CkIndex_Main::quiescence(),thisProxy);
  CkStartQD(cb);

  numQuiescenceRecvd = 0;
  delete msg;
}

static bool hasSuffix(const string &s, const string &suffix){
  if(s.size() < suffix.size()) return false;
  return s.compare(s.size()-suffix.size(), suffix.size(), suffix) == 0;
}

void Main::setParameters(CkArgMsg *m){
  map<string,string> table;
  params.extractParameters(m->argc, m->argv, table); 

  map<string,string>::iterator it = table.find("in");
  if(it == table.end()){
    CkPrintf("[Main] Must specify input particle file:\n");
    usage();
    CkAbort("bad command line arguments\n");
  }

  globalParams.filename = params.getsparam("in", table);

  it = table.find("format");
  if(it == table.end()){
    // No -format given: infer from the extension. A .csv file is the
    // whitepaper's shared text format; anything else is the bundled binary.
    globalParams.inputFormat = hasSuffix(globalParams.filename, ".csv")
                                   ? INPUT_CSV : INPUT_BINARY;
  }
  else if(it->second == "csv"){
    globalParams.inputFormat = INPUT_CSV;
  }
  else if(it->second == "binary" || it->second == "bin"){
    globalParams.inputFormat = INPUT_BINARY;
  }
  else{
    CkPrintf("[Main] unrecognized -format=%s (expected 'csv' or 'binary')\n",
             it->second.c_str());
    usage();
    CkAbort("bad command line arguments\n");
  }
  CkPrintf("format: %s\n",
           globalParams.inputFormat == INPUT_CSV ? "csv" : "binary");

  globalParams.theta = params.getrparam("theta", DEFAULT_THETA, table);
  CkPrintf("theta: %f\n", globalParams.theta);

  globalParams.G = params.getrparam("G", DEFAULT_G, table);
  CkPrintf("G: %g\n", globalParams.G);

  globalParams.dtime = params.getrparam("dtime", DEFAULT_DTIME, table);
  CkPrintf("dtime: %f\n", globalParams.dtime);

  globalParams.dthf = globalParams.dtime/2.0;

  globalParams.epssq = params.getrparam("eps", DEFAULT_EPS, table);
  CkPrintf("eps: %f\n", globalParams.epssq);
  globalParams.epssq = globalParams.epssq*globalParams.epssq;

  globalParams.tolsq = globalParams.theta;
  CkPrintf("tol: %f\n", globalParams.tolsq);
  globalParams.tolsq = globalParams.tolsq*globalParams.tolsq;

  globalParams.ppc = params.getiparam("ppc", DEFAULT_PPC, table); 
  CkPrintf("ppc: %d\n", globalParams.ppc);

  globalParams.ppb = params.getiparam("b", DEFAULT_PPB, table);
  CkPrintf("bucketSize: %d\n", globalParams.ppb);

  globalParams.iterations = params.getiparam("killat", DEFAULT_KILLAT, table);
  CkPrintf("killat: %d\n", globalParams.iterations);
  
  globalParams.cacheLineSize = params.getiparam("chunkdepth", DEFAULT_CHUNK_DEPTH, table);
  CkPrintf("chunkdepth: %d\n", globalParams.cacheLineSize);

  globalParams.yieldPeriod = params.getiparam("yield", DEFAULT_YIELD_PERIOD, table);
  CkPrintf("yieldPeriod: %d\n", globalParams.yieldPeriod);

  getNumParticles();

  it = table.find("p");
  if(it == table.end()){
    globalParams.numTreePieces = ((Real)globalParams.numParticles/((Real)globalParams.ppc))*2.0;
    if(globalParams.numTreePieces == 0) globalParams.numTreePieces = 1;
  }
  else{
    globalParams.numTreePieces = atoi(it->second.c_str());
  }

  CkPrintf("tree pieces: %d\n", globalParams.numTreePieces);


}

void Main::getNumParticles(){
  if(globalParams.inputFormat == INPUT_CSV) scanCsvInput();
  else scanBinaryInput();
}

// Reads the CSV header (if there is one) to learn which column holds what,
// then counts records. Both are one-time, PE 0, I/O-bound costs that land
// before the simulation proper -- the spec excludes input time from the
// reported metrics.
void Main::scanCsvInput(){
  CkPrintf("[Main] file %s\n", globalParams.filename.c_str());
  ifstream partFile(globalParams.filename.c_str(), ios::in | ios::binary);
  CkAssert(partFile.is_open());

  CsvLayout &csv = globalParams.csv;

  string firstLine;
  if(!std::getline(partFile, firstLine)){
    CkAbort("input file is empty\n");
  }

  if(CsvLayout::looksLikeHeader(firstLine)){
    string missing;
    if(!csv.parseHeader(firstLine, missing)){
      CkPrintf("[Main] %s: header names no '%s' column. Expected the columns "
               "of spec v1.0 sec. 5: id,mass,pos_x,pos_y,pos_z,vel_x,vel_y,vel_z\n",
               globalParams.filename.c_str(), missing.c_str());
      CkAbort("unusable CSV header\n");
    }
    csv.dataOffset = (long)partFile.tellg();
  }
  else{
    // No header. Take the spec's column order on faith, but say so, because
    // a misordered headerless file would otherwise run and give wrong answers.
    csv.setSpecOrder();
    csv.dataOffset = 0;
    CkPrintf("[Main] no CSV header found; assuming spec column order "
             "id,mass,pos_x,pos_y,pos_z,vel_x,vel_y,vel_z\n");
  }

  partFile.clear();
  partFile.seekg(0, ios::end);
  globalParams.inputFileSize = (long)partFile.tellg();
  partFile.seekg(csv.dataOffset, ios::beg);

  // Count records by scanning for line terminators rather than by parsing --
  // this pass only needs to know how many there are. Blank lines do not
  // count, matching isBlankCsvLine() in the per-PE readers.
  const size_t BUFSIZE = 1 << 20;
  vector<char> buf(BUFSIZE);
  long long records = 0;
  bool sawContent = false;
  while(partFile){
    partFile.read(&buf[0], BUFSIZE);
    std::streamsize got = partFile.gcount();
    for(std::streamsize i = 0; i < got; i++){
      char c = buf[i];
      if(c == '\n'){
        if(sawContent) records++;
        sawContent = false;
      }
      else if(c != '\r'){
        sawContent = true;
      }
    }
  }
  if(sawContent) records++;   // last line, unterminated

  partFile.close();

  if(records == 0){
    CkAbort("input file contains no particle records\n");
  }
  if(records > (long long)INT_MAX){
    CkAbort("more particles than an int can index\n");
  }
  globalParams.numParticles = (int)records;
  CkPrintf("[Main] numParticles %d\n", globalParams.numParticles);
}

void Main::scanBinaryInput(){
  ifstream partFile;
  CkPrintf("[Main] file %s\n", globalParams.filename.c_str());
  partFile.open(globalParams.filename.c_str(), ios::in | ios::binary);
  CkAssert(partFile.is_open());

  partFile.read((char *)(&globalParams.numParticles),sizeof(int)); 
  CkPrintf("[Main] numParticles %d\n", globalParams.numParticles);

  // The on-disk width of this format follows sizeof(Real), so a file written
  // by an older single-precision build would be silently misread. Check the
  // size before anything seeks into it.
  partFile.seekg(0, ios::end);
  std::streamoff actualSize = partFile.tellg();
  std::streamoff expectedSize =
      (std::streamoff)PREAMBLE_SIZE +
      (std::streamoff)globalParams.numParticles*(std::streamoff)SIZE_PER_PARTICLE;
  if(actualSize != expectedSize){
    CkPrintf("[Main] %s: expected %lld bytes for %d particles at %zu bytes each, found %lld.\n",
             globalParams.filename.c_str(), (long long)expectedSize,
             globalParams.numParticles, (size_t)SIZE_PER_PARTICLE,
             (long long)actualSize);
    CkPrintf("[Main] This build reads/writes %zu-byte reals. Regenerate the input "
             "with the matching ./plummer or ./gen.\n", sizeof(Real));
    CkAbort("input file size does not match this build's particle layout\n");
  }

  partFile.close();
}

void Main::commence(){
  CkPrintf("[Main] load particles\n");
  CkReductionMsg *redMsg;
  dataManagerProxy.loadParticles(CkCallbackResumeThread((void *&)redMsg));

  BoundingBox &universe = *((BoundingBox *)redMsg->getData());
  Real pad = 0.001;
  universe.expand(pad);
  CkPrintf("[Main] universe bb: %f %f %f %f %f %f\n",
                                universe.box.lesser_corner.x,
                                universe.box.lesser_corner.y,
                                universe.box.lesser_corner.z,
                                universe.box.greater_corner.x,
                                universe.box.greater_corner.y,
                                universe.box.greater_corner.z
                                );

  dataManagerProxy.decompose(universe);

}

void Main::reportFinalEnergy(CkReductionMsg *msg){
  BoundingBox &universe = *((BoundingBox *)msg->getData());
  CkPrintf("[ENERGY] step %d E_K %.10g E_P %.10g E_T %.10g\n",
           globalParams.iterations-1,
           universe.kineticEnergy,
           universe.potentialEnergy,
           universe.totalEnergy());
  delete msg;
  niceExit();
}

void Main::niceExit(){
  CkPrintf("[Main] graceful exit\n");
  CkExit();
}

void Main::quiescence(){
  CkPrintf("[Main] Quiescence detected!\n");
  treePieceProxy.quiescence();
  dataManagerProxy.quiescence();
}

void Main::quiescenceExit(){
  numQuiescenceRecvd++;
  if(numQuiescenceRecvd == 2){
    CkExit();
  }
}

string NodeTypeString[] = { 
  "Invalid",
  "Internal",
  "Bucket",
  "EmptyBucket",
  "Boundary",
  "Remote",
  "RemoteBucket",
  "RemoteEmptyBucket"
};

void Main::usage(){
  map<string,string> usage;
  usage["in"] = "input file";
  usage["format"] = "input format: 'csv' (id,mass,pos_x,pos_y,pos_z,vel_x,vel_y,vel_z) "
                    "or 'binary' (./plummer, ./gen). Default: inferred from the extension";
  usage["ppc"] = "particles per chare";
  usage["b"] = "particles per bucket (leaf)";
  usage["theta"] = "opening angle";
  usage["G"] = "gravitational constant, in the units of the input dataset";
  usage["killat"] = "num single steps";
  usage["chunkDepth"] = "when fetching remote data, what depth of subtree to fetch";
  usage["yield"] = "how many buckets to process before yielding processor";
  usage["ppc"] = "particleschare";


  map<string,string>::iterator it;
  CkPrintf("Usage: ./charmrun +p<numproc> ./barnes <options>\n");
  CkPrintf("Options take the form -<name>=<value>\n");
  CkPrintf("Options are described below:\n");
  for(it = usage.begin(); it != usage.end(); it++){
    CkPrintf("%s : %s\n", it->first.c_str(), it->second.c_str());
  }
}

#include "barnes.def.h"

#ifndef __MESSAGES_H__
#define __MESSAGES_H__

#include "Particle.h"
#include "MultipoleMoments.h"
#include "Node.h" 

struct SplitterMsg : public CMessage_SplitterMsg {
  int *splitBins;
  int nSplitBins;
};

struct ParticleMsg : public CMessage_ParticleMsg {
  Particle *part;
  int numParticles;
};

struct RangeMsg : public CMessage_RangeMsg {
  // Two keys per TreePiece -- the lowest and highest Morton key it owns --
  // and, in leafStart, the leaf index each TreePiece's run of the curve begins
  // at, with a final entry equal to the leaf count.
  Key *keys;
  int *leafStart;
  int numTreePieces;
};

struct RequestMsg : public CMessage_RequestMsg {
  Key key;
  int replyTo;

  RequestMsg(Key k, int reply) : 
    key(k), replyTo(reply)
  {
  }
};

struct ParticleReplyMsg : public CMessage_ParticleReplyMsg {
  Key key;
  ExternalParticle *data;
  int np;
};

struct NodeReplyMsg : public CMessage_NodeReplyMsg {
  Key key;
  Node<ForceData> *data;
  int nn;
};

struct MomentsExchangeStruct;
struct MomentsMsg : public CMessage_MomentsMsg {
  MomentsExchangeStruct data;

  MomentsMsg(Node<ForceData> *node) 
  {
    data = (*node);
  }
};

struct RescheduleMsg : public CMessage_RescheduleMsg {
};
#endif

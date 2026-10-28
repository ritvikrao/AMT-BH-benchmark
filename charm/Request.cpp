#include "Request.h"

#include "State.h"
#include "Worker.h"
#include "Traversal_decls.h"
#include "Node.h"

#include "TreePiece.h"

// Resume each walk that waited for this leaf, for the buckets that reached
// it, and flush what it adds (gravity.h: every walk ends with a flush).
void Request::deliverParticles(int num){
  for(int i = 0; i < requestors.length(); i++){
    Requestor &req = requestors[i];
    TraversalWorker *worker = req.worker;
    TreePiece *owner = worker->getOwner();
    owner->startLoad();

    worker->setContext(req.context);

    ExternalParticle *externalParticles = (ExternalParticle *)data;
    for(int j = 0; j < num; j++){ 
      CkAssert(!isnan((externalParticles+j)->position.length()));
    }
    worker->applyParticles(externalParticles,num,req.context.mask);
    worker->endWalk();
    State *state = req.state;
    state->groupRequestDone(state->groupIndex(req.context.group));
    if(state->decrPending()){
      worker->done();
    }
    owner->stopLoad();
  }

  requestors.clear();
}

void Request::deliverNode(){
  for(int i = 0; i < requestors.length(); i++){
    Requestor &req = requestors[i];
    TraversalWorker *worker = req.worker;
    TreePiece *owner = worker->getOwner();
    owner->startLoad();

    worker->setContext(req.context);

    Traversal<ForceData> *traversal = req.traversal; 
    State *state = req.state;
    Node<ForceData> *firstChild = (Node<ForceData>*) data;
    // start the traversal for each of the received children
    for(int j = 0; j < BRANCH_FACTOR; j++){
      traversal->gravityWalk(firstChild+j,worker,state,req.context.mask);
    }
    worker->endWalk();
    state->groupRequestDone(state->groupIndex(req.context.group));
    if(state->decrPending()){
      worker->done();
    }
    owner->stopLoad();
  }

  requestors.clear();

}

#include "Traversal_defs.h"

#include "ai-strategy-round49e-planning.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
void projectAIRound49ePlanning(AIRequest&r,Room&room,const ServerPlayer*p){
 auto facts=r.worldView.strategyFacts.value("kearmsxfyli").toObject();
 facts.remove("kesxchunlaoToGetCount");
 r.worldView.strategyFacts.insert("kearmsxfyli",facts);
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.stateRevision!=room.roomRuntime()->stateRevision()
  ||r.kind!=AIRequest::PlayerChosen||r.choiceOptions.reason!="kesxchunlao"
  ||r.choiceOptions.question!="askForPlayerChosen")return;
 // Fixed owner tag records its own discarded IDs, including duplicates. Source
 // only observes IntList.length(); do not expose IDs, moved faces or other tags.
 // QVariant::toIntList uses value<QList<int>>: absent/wrong type means empty.
 facts.insert("kesxchunlaoToGetCount",p->getTag("kesxchunlaoToGet").value<QList<int>>().size());
 r.worldView.strategyFacts.insert("kearmsxfyli",facts);
}

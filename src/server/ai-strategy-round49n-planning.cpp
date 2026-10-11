#include "ai-strategy-round49n-planning.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
void projectAIRound49nPlanning(AIRequest&r,Room&room,const ServerPlayer*p){
 auto package=r.worldView.strategyFacts.value("gaoda").toObject();
 auto props=package.value("owner_properties").toObject();props.remove("luaqiangwucolor");
 package.insert("owner_properties",props);r.worldView.strategyFacts.insert("gaoda",package);
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.stateRevision!=room.roomRuntime()->stateRevision()||r.kind!=AIRequest::RespondCard
  ||r.choiceOptions.question!="askForCard"||r.pattern!="..!"||r.prompt!="@luaqiangwu"
  ||r.handlingMethod!=Card::MethodDiscard)return;
 // Fixed Gaoda owner property, cleared at TurnStart and written after own Shoot.
 // The callback reads only toString: absent QVariant is the actual empty string.
 // No current skill/mark/color filter is added before this source read.
 props.insert("luaqiangwucolor",p->property("luaqiangwucolor").toString());
 package.insert("owner_properties",props);r.worldView.strategyFacts.insert("gaoda",package);
}

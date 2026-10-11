#include "ai-strategy-round49p-planning.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
void projectAIRound49pPlanning(AIRequest&r,Room&room,const ServerPlayer*p){
 auto facts=r.worldView.strategyFacts.value("sgs10th").toObject();facts.remove("ny_10th_xiaoyan");
 r.worldView.strategyFacts.insert("sgs10th",facts);
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.stateRevision!=room.roomRuntime()->stateRevision()||r.kind!=AIRequest::Discard
  ||r.choiceOptions.question!="askForDiscard"||r.choiceOptions.reason!="ny_10th_xiaoyan"
  ||r.choiceOptions.minCount!=1||r.choiceOptions.maxCount!=1||!r.choiceOptions.optional
  ||!r.choiceOptions.context.value("exchange").toBool()
  ||!r.choiceOptions.context.value("include_equip").toBool())return;
 // The fixed GameStart producer publicly logs its actor and deals damage before
 // storing that actor, then asks each recipient this exact optional HE exchange.
 // Observe the actual tag, not a reconstructed prompt target or hidden card.
 auto*target=room.getTag("ny_10th_xiaoyan").value<ServerPlayer*>();
 if(!target||!room.getAllPlayers(true).contains(target))return;
 facts.insert("ny_10th_xiaoyan",target->objectName());r.worldView.strategyFacts.insert("sgs10th",facts);
}

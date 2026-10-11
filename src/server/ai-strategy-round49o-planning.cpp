#include "ai-strategy-round49o-planning.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
void projectAIRound49oPlanning(AIRequest&r,Room&room,const ServerPlayer*p){
 auto facts=r.worldView.strategyFacts.value("sgs10th").toObject();
 facts.remove("ny_10th_zhuangpo");r.worldView.strategyFacts.insert("sgs10th",facts);
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.stateRevision!=room.roomRuntime()->stateRevision()||r.kind!=AIRequest::SkillInvoke
  ||r.choiceOptions.question!="askForSkillInvoke"||r.choiceOptions.reason!="ny_10th_zhuangpo")return;
 // Fixed TargetConfirmed producer sets this owner's own tag to the actual
 // ordered use.to member immediately before this inquiry. No prompt inference,
 // additional hasSkill test, card-face exposure, or payment authorization.
 auto*target=p->getTag("ny_10th_zhuangpo").value<ServerPlayer*>();
 if(!target||!room.getAllPlayers(true).contains(target))return;
 facts.insert("ny_10th_zhuangpo",target->objectName());
 r.worldView.strategyFacts.insert("sgs10th",facts);
}

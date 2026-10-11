#include "ai-strategy-round49h-planning.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
void projectAIRound49hPlanning(AIRequest&r,Room&room,const ServerPlayer*p){
 auto gaoda=r.worldView.strategyFacts.value("gaoda").toObject();gaoda.remove("G_SELF_HT_skill");
 auto tenth=r.worldView.strategyFacts.value("sgs10th").toObject();tenth.remove("ny_tenth_shizong_from");
 r.worldView.strategyFacts.insert("gaoda",gaoda);r.worldView.strategyFacts.insert("sgs10th",tenth);
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.stateRevision!=room.roomRuntime()->stateRevision())return;
 const bool backpack=r.kind==AIRequest::SkillInvoke&&r.choiceOptions.question=="askForSkillInvoke"
  &&r.choiceOptions.reason=="#G_SELF_HT_skill";
 const bool exchange=r.kind==AIRequest::Discard&&r.choiceOptions.question=="askForDiscard"
  &&r.choiceOptions.reason=="ny_tenth_shizong";
 if(!backpack&&!exchange)return;
 const QString key=backpack?QStringLiteral("G_SELF_HT_skill"):QStringLiteral("ny_tenth_shizong_from");
 auto*target=p->getTag(key).value<ServerPlayer*>();
 // Both fixed producers assign a nonnull existing source/target immediately
 // before their question. A null or non-roster pointer is outside that proved
 // writer domain: leave unknown, never fabricate a target or relation false.
 if(!target||!room.getAllPlayers(true).contains(target))return;
 if(backpack){gaoda.insert(key,target->objectName());r.worldView.strategyFacts.insert("gaoda",gaoda);}
 else{tenth.insert(key,target->objectName());r.worldView.strategyFacts.insert("sgs10th",tenth);}
}

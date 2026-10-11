#include "ai-strategy-round49-gongjian.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonArray>
void projectAIRound49Gongjian(AIRequest&q,Room&r,ServerPlayer*p){
 auto f=q.worldView.strategyFacts.value("tenyear_hc").toObject();f.remove("gongjianData");f.remove("gongjian_slash_targets");
 const auto publish=[&]{if(f.isEmpty())q.worldView.strategyFacts.remove("tenyear_hc");else q.worldView.strategyFacts.insert("tenyear_hc",f);};publish();
 if(!p||!r.getPlayers().contains(p)||p->getRoom()!=&r||!r.roomRuntime()||r.roomRuntime()->isClosing()||!q.decisionId||q.viewerObjectName!=p->objectName()||q.worldView.self.objectName!=p->objectName()||q.stateRevision!=r.roomRuntime()->stateRevision()||q.kind!=AIRequest::SkillInvoke||q.choiceOptions.reason!="gongjian"||q.choiceOptions.question!="askForSkillInvoke")return;
 // Fixed Gongjian::trigger stores TargetSpecified data in this owner's tag
 // immediately before this question. The AI reads ONLY to:list, never card/from.
 // Preserve QVariant::value's empty CardUseStruct and resulting source nil error.
 const auto use=p->getTag("gongjianData").value<CardUseStruct>();QJsonArray to;
 for(auto*t:use.to){if(!t||!r.getPlayers().contains(t)||t->getRoom()!=&r)return;to.append(t->objectName());}
 f.insert("gongjianData",QJsonObject{{"to",to}});
 // Only fixed native writer is the public CardFinished target-name list.
 // This is current public room metadata, not a retained private policy slot.
 f.insert("gongjian_slash_targets",QJsonArray::fromStringList(r.getTag("gongjian_slash_targets").toStringList()));publish();
}

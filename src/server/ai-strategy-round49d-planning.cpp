#include "ai-strategy-round49d-planning.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "structs.h"
void projectAIRound49dPlanning(AIRequest&r,Room&room,const ServerPlayer*p){
 auto facts=r.worldView.strategyFacts.value("shixinrumo_yi").toObject();facts.remove("chendancuiData");
 r.worldView.strategyFacts.insert("shixinrumo_yi",facts);
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.stateRevision!=room.roomRuntime()->stateRevision()||r.choiceOptions.reason!="chendancui")return;
 if(!((r.kind==AIRequest::SkillInvoke&&r.choiceOptions.question=="askForSkillInvoke")
  ||(r.kind==AIRequest::Discard&&r.choiceOptions.question=="askForDiscard")))return;
 // The fixed compulsory branch intentionally DOES NOT overwrite this owner
 // tag. Preserve its stored target, not the current event/prompt target.
 // QVariant::toDamage is value<DamageStruct>(); absent/wrong type yields a
 // default structure with null to. No card/from/amount/nature is exposed.
 const auto d=p->getTag("chendancuiData").value<DamageStruct>();QJsonObject row;
 if(d.to){if(!room.getAllPlayers(true).contains(d.to))return;row.insert("to",d.to->objectName());}
 facts.insert("chendancuiData",row);r.worldView.strategyFacts.insert("shixinrumo_yi",facts);
}

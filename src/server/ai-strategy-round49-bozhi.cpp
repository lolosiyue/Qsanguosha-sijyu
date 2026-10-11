#include "ai-strategy-round49-bozhi.h"
#include "ai.h"
#include "card.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <QSet>
namespace {
bool question(const AIRequest&r,Room&room,const ServerPlayer*p){
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.stateRevision!=room.roomRuntime()->stateRevision()||!room.getPlayers().contains(const_cast<ServerPlayer*>(p)))return false;
 const bool invoke=r.kind==AIRequest::SkillInvoke&&r.choiceOptions.question=="askForSkillInvoke"&&r.choiceOptions.reason=="yhbozhi";
 const bool response=r.kind==AIRequest::UseCard&&r.pattern=="@@yhbozhi"
  &&(r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE||r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE);
 if(!invoke&&!response)return false;
 for(const auto&i:p->getSkillInstances())if(i.skillName=="yhbozhi")return true;
 return false;
}
}
void projectAIRound49Bozhi(AIRequest&r,Room&room,ServerPlayer*p){
 auto row=r.worldView.strategyFacts.value("owner_skill_properties").toObject();row.remove("YHBozhiRecordTrick");
 if(question(r,room,p))row.insert("YHBozhiRecordTrick",p->property("YHBozhiRecordTrick").toString());
 if(row.isEmpty())r.worldView.strategyFacts.remove("owner_skill_properties");else r.worldView.strategyFacts.insert("owner_skill_properties",row);
}
bool aiRound49BozhiConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 if(!question(r,room,p))return false;
 const QSet<QString>keys{"source_key","skill","name","suit","number","subcards"};
 if(s.size()!=keys.size())return false;for(auto i=s.begin();i!=s.end();++i)if(!keys.contains(i.key()))return false;
 const auto projected=r.worldView.strategyFacts.value("owner_skill_properties").toObject().value("YHBozhiRecordTrick");
 if(!projected.isString()||projected.toString()!=p->property("YHBozhiRecordTrick").toString())return false;
 // Fixed yinhu-ai.lua1005–1028 constructs before availability/recast checks.
 // This attests a current owner source name, including empty -> nil clone.
 // It does not query hidden history, native canActivate or mint payment.
 return s.value("source_key")==QJsonValue("yhbozhi")&&s.value("skill")==QJsonValue("yhbozhi")
  &&s.value("name").isString()&&s.value("name")==projected
  &&s.value("suit")==QJsonValue(int(Card::NoSuit))&&s.value("number")==QJsonValue(0)
  &&s.value("subcards").isArray()&&s.value("subcards").toArray().isEmpty();
}

#include "ai-strategy-round49-zero.h"
#include "ai.h"
#include "card.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <QSet>
bool aiRound49ZeroConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.stateRevision!=room.roomRuntime()->stateRevision()||!room.getPlayers().contains(const_cast<ServerPlayer*>(p)))return false;
 const QSet<QString>keys{"source_key","skill","name","suit","number","subcards"};
 if(s.size()!=keys.size())return false;for(auto i=s.begin();i!=s.end();++i)if(!keys.contains(i.key()))return false;
 if(s.value("suit")!=QJsonValue(int(Card::NoSuit))||s.value("number")!=QJsonValue(0)
  ||!s.value("subcards").isArray()||!s.value("subcards").toArray().isEmpty())return false;
 const QString key=s.value("source_key").toString();
 // Exact reached source inquiry only. Recipients of Jianji/Suizheng need not own
 // the skill. No phase, use-count, availability or payment grant is added.
 if(key=="@@jianjiyh")return r.kind==AIRequest::UseCard&&r.pattern=="@@jianjiyh"
  &&(r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE||r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE)
  &&s.value("skill")==QJsonValue("_jianjiyh")&&s.value("name")==QJsonValue("slash");
 if(key=="suizheng1")return r.kind==AIRequest::PlayerChosen&&r.choiceOptions.question=="askForPlayerChosen"
  &&r.choiceOptions.reason==key&&s.value("skill")==QJsonValue("suizheng")&&s.value("name")==QJsonValue("slash");
 if(key!="s2_fanji"&&key!="s2_yongbing")return false;
 return r.kind==AIRequest::SkillInvoke&&r.choiceOptions.question=="askForSkillInvoke"
  &&r.choiceOptions.reason==key&&s.value("skill")==QJsonValue(key)
  &&s.value("name")==QJsonValue(key=="s2_fanji"?"slash":"amazing_grace");
}

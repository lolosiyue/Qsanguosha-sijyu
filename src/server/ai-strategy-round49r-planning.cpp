#include "ai-strategy-round49r-planning.h"
#include "ai-strategy-round44-certificate.h"
#include "ai.h"
#include "card.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <QSet>
bool aiRound49rHfgushiConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!r.pattern.isEmpty()
  ||!aiRound44RangeRequestCurrent(room,r,p))return false;
 const QSet<QString>fields{"source_key","name","skill","suit","number","subcards"};
 if(s.size()!=fields.size())return false;for(auto i=s.begin();i!=s.end();++i)if(!fields.contains(i.key()))return false;
 if(s.value("source_key")!="hfgushi"||s.value("skill")!="hfgushi"
  ||(s.value("name")!="peach"&&s.value("name")!="ex_nihilo"&&s.value("name")!="duel")
  ||!s.value("subcards").isArray()||!s.value("subcards").toArray().isEmpty())return false;
 const bool first=s.value("suit")==QJsonValue(int(Card::SuitToBeDecided))&&s.value("number")==QJsonValue(-1);
 const bool parsed=s.value("suit")==QJsonValue(int(Card::NoSuit))&&s.value("number")==QJsonValue(0);
 if(!first&&!parsed)return false;
 // Exact fixed source's two allocations. Current own public mark chooses the
 // name in Lua; this grants all three readonly hypotheses, not availability,
 // activation, mark change, selected payment or a foreign owner's rule facts.
 for(const auto&i:p->getSkillInstances())if(i.skillName=="hfgushi")return true;
 return false;
}

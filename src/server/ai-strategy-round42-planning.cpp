#include "ai-strategy-round42-planning.h"
#include "ai-strategy-construction-query.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <QSet>
namespace {
bool inquiry(const AIRequest&r,Room&room,const ServerPlayer*p,const QString&skill){
 return p&&p->getRoom()==&room&&!room.roomRuntime()->isClosing()&&r.decisionId!=0
  &&r.viewerObjectName==p->objectName()&&r.worldView.self.objectName==p->objectName()
  &&r.stateRevision==room.roomRuntime()->stateRevision()&&p->hasSkill(skill)
  &&r.kind==AIRequest::UseCard&&r.pattern=="@@"+skill
  &&(r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE||r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE);
}
}
bool aiRound42PlanningConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 static const QSet<QString>fields{"source_key","name","skill","suit","number","subcards"};
 if(s.size()!=fields.size())return false;
 for(auto it=s.begin();it!=s.end();++it)if(!fields.contains(it.key()))return false;
 if(!s.value("source_key").isString()||!s.value("name").isString()||!s.value("skill").isString()
  ||!s.value("suit").isDouble()||s.value("suit").toDouble()!=int(Card::NoSuit)
  ||!s.value("number").isDouble()||s.value("number").toDouble()!=0
  ||!s.value("subcards").isArray()||!s.value("subcards").toArray().isEmpty())return false;
 const QString key=s.value("source_key").toString();
 QString source,raw,name;
 if(key=="newgenerals.moulveying"){source=raw="moulveying";name="dismantlement";}
 else if(key=="newgenerals.mouyingwu"){source=raw="mouyingwu";name="slash";}
 else if(key=="newgenerals.mouxieji"){source="mouxieji";raw="_mouxieji";name="slash";}
 else return false;
 return s.value("name")==QJsonValue(name)&&s.value("skill")==QJsonValue(raw)&&inquiry(r,room,p,source);
}
int aiRound42PlanningExtraTarget(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 return s.value("source_key")==QJsonValue("newgenerals.mouxieji")&&aiRound42PlanningConstruction(r,room,p,s)?2:0;
}
bool aiRound42PlanningSource(const AIRequest&r,Room&room,const ServerPlayer*p,const Card*c,const QString&key){
 if(!c||key!="newgenerals.mouxieji"||c->getId()>=0||!c->isVirtualCard()
  ||c->objectName()!="slash"||c->getSkillName(false)!="_mouxieji"
  ||c->getSuit()!=Card::NoSuit||c->getNumber()!=0||!c->getSubcards().isEmpty())return false;
 return aiRound42PlanningConstruction(r,room,p,{{"source_key",key},{"name","slash"},{"skill","_mouxieji"},
  {"suit",int(Card::NoSuit)},{"number",0},{"subcards",QJsonArray{}}});
}
QJsonObject aiRound42PlanningTargetFilter(Room&room,ServerPlayer*p,const AIRequest&r,const Card*c,const QString&key,const QJsonObject&q){
 auto reject=[](const char*s){return QJsonObject{{"error",s},{"key","newgenerals.mouxieji"}};};
 if(!aiRound42PlanningSource(r,room,p,c,key))return reject("Mouxieji readonly source identity changed");
 if(q.size()!=2||!q.value("prefix").isArray()||!q.value("target").isString())return reject("Mouxieji prefix query malformed");
 QList<const Player*>prefix;QSet<QString>seen;
 const auto names=q.value("prefix").toArray();if(names.size()>room.getAlivePlayers().size())return reject("Mouxieji prefix exceeds live roster");
 for(const auto&n:names){
  if(!n.isString()||seen.contains(n.toString()))return reject("Mouxieji prefix is not distinct");
  auto*to=room.findPlayerByObjectName(n.toString());if(!to||!to->isAlive())return reject("Mouxieji prefix player absent");
  seen.insert(n.toString());prefix<<to;
 }
 auto*to=room.findPlayerByObjectName(q.value("target").toString());if(!to||!to->isAlive())return reject("Mouxieji target absent");
 const auto revision=room.roomRuntime()->stateRevision();
 AITianyiQueryScope scope(p,c,2,false);const bool accepted=c->targetFilter(prefix,to,p);
 if(revision!=room.roomRuntime()->stateRevision())return reject("Mouxieji readonly query changed state");
 return{{"accepted",accepted}};
}
int aiRound42PlanningPrototype(const AIRequest&r,const ServerPlayer*p,const QString&skill,const QString&prototype){
 if(!p||!p->getRoom()||skill!="mouxieji"||prototype!="#mouxiejiCard"||!inquiry(r,*p->getRoom(),p,skill))return -1;
 // Fixed source returns an empty, undeclared SkillCard after Slash simulation.
 // Existing native instance/prefix/factory/final checks still admit or reject it.
 return 0;
}

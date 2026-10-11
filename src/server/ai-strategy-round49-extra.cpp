#include "ai-strategy-round49-extra.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-card-identity.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <QSet>
#include <memory>
namespace {
bool owns(const ServerPlayer*p,const QString&skill){
 for(const auto&i:p->getSkillInstances())if(i.skillName==skill)return true;
 return false;
}
bool faceCurrent(Room&room,const ServerPlayer*p,const AICardView&v,const Card*c){
 return c&&v.effectiveId==c->getEffectiveId()&&room.getCardOwner(v.effectiveId)==p
  &&v.objectName==c->objectName()&&v.className==c->getClassName()
  &&v.suit==int(c->getSuit())&&v.number==c->getNumber()
  &&!v.legacyIdentityToken.isEmpty()
  &&v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c);
}
const AICardView* gensouFirst(const AIRequest&r,Room&room,const ServerPlayer*p){
 const auto ids=p->getPile("gensou");if(ids.isEmpty())return nullptr;
 // Do not substitute the current native loop element for the AI's first().
 for(const auto&pile:r.worldView.self.piles)if(pile.name=="gensou")
  for(const auto&v:pile.cards)if(v.effectiveId==ids.first()
   &&faceCurrent(room,p,v,Sanguosha->getCard(ids.first())))return &v;
 return nullptr;
}
bool play(const AIRequest&r){return r.kind==AIRequest::Activate
 &&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&r.pattern.isEmpty();}
}
bool aiRound49ExtraConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||!aiRound44RangeRequestCurrent(room,r,p))return false;
 const QSet<QString>fields{"source_key","skill","name","suit","number","subcards"};
 if(s.size()!=fields.size())return false;
 for(auto i=s.begin();i!=s.end();++i)if(!fields.contains(i.key()))return false;
 if(!s.value("source_key").isString()||!s.value("skill").isString()||!s.value("name").isString()
  ||!s.value("suit").isDouble()||!s.value("number").isDouble()||!s.value("subcards").isArray())return false;
 const QString key=s.value("source_key").toString(),skill=s.value("skill").toString(),name=s.value("name").toString();
 const auto costs=s.value("subcards").toArray();
 const bool zero=s.value("suit")==QJsonValue(int(Card::NoSuit))&&s.value("number")==QJsonValue(0);
 if(key=="@s2_jizhan-exchange")return owns(p,"s2_jizhan")
  &&r.kind==AIRequest::RespondCard&&r.choiceOptions.question=="askForCard"
  &&r.prompt.startsWith("@s2_jizhan-exchange:")&&r.handlingMethod==Card::MethodDiscard
  &&name=="slash"&&skill=="s2_jizhan"&&zero&&costs.isEmpty();
 if(key=="LuaZhenlie")return owns(p,key)&&r.kind==AIRequest::Choice
  &&r.choiceOptions.question=="askForChoice"&&r.choiceOptions.reason==key
  &&name=="fire_slash"&&skill==key&&zero&&costs.isEmpty();
 if(key=="genjutsu"){
  if(!owns(p,key)||r.kind!=AIRequest::PlayerChosen||r.choiceOptions.question!="askForPlayerChosen"
   ||r.choiceOptions.reason!=key||name!="slash")return false;
  const auto*v=gensouFirst(r,room,p);if(!v||s.value("suit")!=QJsonValue(v->suit)
   ||s.value("number")!=QJsonValue(v->number))return false;
  // clone -> addSubcard -> setSkillName: all actual intermediate products.
  if(costs.isEmpty())return skill.isEmpty();
  return costs.size()==1&&costs.first()==QJsonValue(v->effectiveId)
   &&(skill.isEmpty()||skill=="genjutsuslash");
 }
 if(key=="#s2_longhun"){
  static const QSet<QString>names{"slash","fire_slash","thunder_slash","peach"};
  if(!play(r)||!owns(p,"s2_longhun")||!names.contains(name)||!costs.isEmpty()
   ||(!skill.isEmpty()&&skill!="s2_longhun"))return false;
  if(zero)return true; // empty native SkillCard defaults
  const auto matches=[&](const AICardView&v){const Card*c=Sanguosha->getCard(v.effectiveId);
   return faceCurrent(room,p,v,c)&&c->isKindOf("BasicCard")
    &&s.value("suit")==QJsonValue(v.suit)&&s.value("number")==QJsonValue(v.number);};
  for(const auto&v:r.worldView.handCards)if(p->handCards().contains(v.effectiveId)&&matches(v))return true;
  for(const auto&pile:r.worldView.self.piles)if(pile.name=="wooden_ox")
   for(const auto&v:pile.cards)if(p->getPile("wooden_ox").contains(v.effectiveId)&&matches(v))return true;
  return false;
 }
 if(key=="#genshunoYURICard"){
  if(!play(r)||!owns(p,"genshunoYURI")||skill!="genshunoYURI"||!zero||!costs.isEmpty())return false;
  bool listed=false;for(const auto&v:r.worldView.strategyFacts.value("pattern_names").toArray())
   if(v.isString()&&v.toString()==name){listed=true;break;}
  if(!listed)return false;
  std::unique_ptr<Card>c(Sanguosha->cloneCard(name,Card::NoSuit,0));
  return c&&(c->isKindOf("BasicCard")||c->isNDTrick());
 }
 return false;
}
bool aiRound49ExtraPileMaterial(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s,int id){
 return s.value("source_key")==QJsonValue("genjutsu")&&aiRound49ExtraConstruction(r,room,p,s)
  &&s.value("subcards").toArray().size()==1&&s.value("subcards").toArray().first()==QJsonValue(id);
}

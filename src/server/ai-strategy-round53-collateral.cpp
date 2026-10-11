#include "ai-strategy-round53-collateral.h"
#include "ai-strategy-round50-effects.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-card-identity.h"
#include "ai.h"
#include "ai-runtime.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QPointer>
#include <QHash>
#include <QJsonArray>
#include <atomic>
namespace {
thread_local QHash<Room*,QList<AIRound53CollateralEffect*>> effects;
thread_local QHash<Room*,QList<AIRound53CollateralQuestion*>> questions;
std::atomic<quint64> serial{0};
QJsonObject fail(const char*s){return{{"known",false},{"reason",QString::fromLatin1(s)}};}
bool current(Room&room,const AIRequest&r,ServerPlayer*p){return p&&p->getRoom()==&room&&r.decisionId&&r.conversionLedger&&r.viewerObjectName==p->objectName()&&r.worldView.self.objectName==p->objectName()&&room.roomRuntime()&&!room.roomRuntime()->isClosing()&&aiRound44RangeRequestCurrent(room,r,p);}
const Card*sourceCard(Room&room,const AIRequest&r,ServerPlayer*p,const QJsonObject&s){
 if(s.size()==1&&s.value("readonly_recipe_key").isString())return room.roomRuntime()->ai().readonlyCard(r,p,s.value("readonly_recipe_key").toString());
 if(s.size()!=2||!s.value("physical_id").isDouble()||!s.value("legacy_identity_token").isString())return nullptr;
 const int id=s.value("physical_id").toInt(-1);if(id<0||s.value("physical_id").toDouble()!=id||!p->handCards().contains(id))return nullptr;
 const Card*c=room.getCard(id);if(!c)return nullptr;
 for(const auto&v:r.worldView.handCards)if(v.effectiveId==id&&v.legacyIdentityToken==s.value("legacy_identity_token").toString()&&!v.legacyIdentityToken.isEmpty()
  &&v.legacyIdentityToken==aiLegacyCardIdentity(room,p,c)&&v.objectName==c->objectName()&&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber())return c;
 return nullptr;
}
}
struct AIRound53CollateralEffect::Data {Room*room;QPointer<ServerPlayer>killer,victim,user;const Card*card=nullptr;QString prompt;bool valid=false,bound=false;};
struct AIRound53CollateralQuestion::Data {Room*room;AIRound53CollateralEffect*effect=nullptr;quint64 serial=0;};
struct AIRound53CollateralAccess {
 static AIRound53CollateralQuestion::Data*question(Room&room){auto it=questions.constFind(&room);return it==questions.cend()||it->isEmpty()?nullptr:it->last()->d.get();}
 static ServerPlayer*user(Room&room,const AIRequest&r,ServerPlayer*p){auto*q=question(room);if(!q||!q->effect||!q->serial||q->serial!=r.round53CollateralQuestionSerial)return nullptr;
  auto it=effects.constFind(&room);if(it==effects.cend()||it->isEmpty()||it->last()!=q->effect)return nullptr;const auto*d=q->effect->d.get();
  return d->valid&&d->killer==p&&d->victim&&d->user&&r.kind==AIRequest::UseCard&&r.pattern=="slash"&&r.prompt==d->prompt&&r.handlingMethod==Card::MethodUse&&r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&current(room,r,p)?d->user.data():nullptr;
 }
};
AIRound53CollateralEffect::AIRound53CollateralEffect(Room&room,ServerPlayer*k,ServerPlayer*v,ServerPlayer*u,const Card*c,const QString&prompt):d(new Data){
 d->room=&room;d->killer=k;d->victim=v;d->user=u;d->card=c;d->prompt=prompt;
 d->valid=k&&v&&u&&k->getRoom()==&room&&v->getRoom()==&room&&u->getRoom()==&room&&c&&c->isKindOf("Collateral")&&prompt=="collateral-slash:"+v->objectName()+":"+u->objectName();effects[&room].append(this);
}
AIRound53CollateralEffect::~AIRound53CollateralEffect(){auto&v=effects[d->room];if(!v.isEmpty()&&v.last()==this)v.removeLast();else v.clear();if(v.isEmpty())effects.remove(d->room);}
AIRound53CollateralQuestion::AIRound53CollateralQuestion(Room&room,ServerPlayer*p,const QString&pattern,const QString&prompt,int method):d(new Data){
 d->room=&room;auto it=effects.find(&room);if(it!=effects.end()&&!it->isEmpty()){
  auto*e=it->last();auto*ed=e->d.get();if(ed->valid&&!ed->bound&&ed->killer==p&&pattern=="slash"&&prompt==ed->prompt&&method==int(Card::MethodUse)){ed->bound=true;d->effect=e;d->serial=++serial;}
 }questions[&room].append(this);
}
AIRound53CollateralQuestion::~AIRound53CollateralQuestion(){auto&v=questions[d->room];if(!v.isEmpty()&&v.last()==this)v.removeLast();else v.clear();if(v.isEmpty())questions.remove(d->room);}
void projectAIRound53Collateral(AIRequest&r,Room&room,ServerPlayer*p){
 r.round53CollateralQuestionSerial=0;
 // This key is projected only for the genuine compiled Collateral inquiry.
 // Clear only this finite field; retain unrelated mode-policy observations.
 auto rows=r.worldView.modePolicy.value("strategy_flags").toObject();
 for(auto it=rows.begin();it!=rows.end();++it){auto row=it.value().toObject();row.remove("SlashAssignee");it.value()=row;}
 r.worldView.modePolicy.insert("strategy_flags",rows);
 auto*q=AIRound53CollateralAccess::question(room);if(!q)return;
 r.round53CollateralQuestionSerial=q->serial;
 if(!AIRound53CollateralAccess::user(room,r,p)){r.round53CollateralQuestionSerial=0;return;}
 // Fixed native/extension writes of this literal all use setPlayerFlag, which
 // broadcasts the value. Preserve all current flags, including a preexisting
 // second assignee; the donor still selects in getOtherPlayers order.
 for(auto*target:room.getAllPlayers(true)){
  auto row=rows.value(target->objectName()).toObject();
  row.insert("SlashAssignee",target->getFlagList().contains("SlashAssignee"));
  rows.insert(target->objectName(),row);
 }
 r.worldView.modePolicy.insert("strategy_flags",rows);
}
QJsonObject queryAIRound53Collateral(Room&room,const AIRequest&r,ServerPlayer*p,const QJsonObject&s){
 if(!current(room,r,p))return fail("Collateral flag query requires exact current owner request");
 if(s.size()!=4||s.value("source")!="ordinary_collateral"||!s.value("op").isString()||!s.value("target").isString()||(!s.value("card").isObject()&&s.value("card")!=QJsonValue(false)))return fail("Collateral flag schema differs");
 const auto op=s.value("op").toString();ServerPlayer*target=nullptr;bool value=false;
 if(op=="set"){
  if(s.value("target")!=p->objectName())return fail("Collateral planning writer is owner-only");
  const Card*c=sourceCard(room,r,p,s.value("card").toObject());if(!c||!c->isKindOf("Collateral"))return fail("Collateral planning writer lacks exact current source Card");
  target=p;value=true; // Donor sets even in dummy planning; repeated true is a native no-op.
 }else if(op=="read"||op=="clear"){
  target=AIRound53CollateralAccess::user(room,r,p);
  if(!target||target->objectName()!=s.value("target").toString()||s.value("card")!=QJsonValue(false))return fail("Collateral foreign flag lacks actual compiled effect/question witness");
  if(op=="read"){const bool value=target->getFlagList().contains("needCrossbow");if(!current(room,r,p))return fail("Collateral flag read changed current");return{{"known",true},{"value",value}};}
 }else return fail("Collateral flag operation outside finite source catalog");
 if(!current(room,r,p))return fail("Collateral flag changed before source setter");
 AIRound50EffectPermit permit(room,r,p,target,QStringLiteral("needCrossbow"),false,value);QString error;
 if(!writeAIRound50Effect(room,r,p,target,permit,&error))return{{"known",false},{"reason",error}};
 return{{"known",true}};
}

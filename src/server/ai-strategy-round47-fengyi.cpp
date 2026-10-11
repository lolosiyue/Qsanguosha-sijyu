#include "ai-strategy-round47-fengyi.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-readonly-card-store.h"
#include "ai.h"
#include "ai-runtime.h"
#include "room.h"
#include "room-runtime.h"
#include "room-state.h"
#include "serverplayer.h"
#include "engine.h"
#include "card.h"
#include <QHash>
#include <QPointer>
#include <QJsonObject>
namespace {
thread_local QHash<Room*,QList<AIRound47FengyiQuestion*>> frames;
}
struct AIRound47FengyiQuestion::Data {
 Room *room=nullptr;QPointer<ServerPlayer> owner,judgeWho;
 const QVariant *data=nullptr;JudgeStruct *judge=nullptr;const Card *judgeCard=nullptr;
 AIReadonlyCardLease judgeLease,selectedLease;
 QString prompt,reason,pattern;bool good=false,judgeResult=false,valid=false,attempted=false,consumed=false;
 quint64 decision=0,revision=0,gameGeneration=0,isolatedGeneration=0;int selected=-1;
 const void *gameState=nullptr,*isolatedState=nullptr;
};
struct AIRound47FengyiAccess {
 static AIRound47FengyiQuestion::Data *top(Room&r){auto i=frames.find(&r);return i==frames.end()||i->isEmpty()?nullptr:i->last()->d.get();}
 static bool current(Room&r,const AIRequest&q,ServerPlayer*p,AIRound47FengyiQuestion::Data*d){
  const auto *runtime=r.roomRuntime();
  if(!runtime||runtime->isClosing()||!d)return false;
  const auto &game=runtime->lua();const auto &isolated=runtime->ai().lua();
  if(game.isClosing()||game.isClosed()||isolated.isClosing()||isolated.isClosed()
   ||!d->gameState||!d->isolatedState||game.rawState()!=d->gameState||isolated.rawState()!=d->isolatedState
   ||game.generation()!=d->gameGeneration||isolated.generation()!=d->isolatedGeneration)return false;
  if(!d||top(r)!=d||!d->valid||!p||d->owner!=p||p->getRoom()!=&r||!p->isAlive()
   ||q.kind!=AIRequest::RespondCard||q.choiceOptions.question!="askForCard"
   ||q.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE||q.handlingMethod!=Card::MethodResponse
   ||q.pattern!="@@sijyu_fengyi"||q.prompt!=d->prompt||q.viewerObjectName!=p->objectName()
   ||!aiRound44RangeRequestCurrent(r,q,p)||!d->data||!d->data->canConvert<JudgeStruct*>()
   ||d->data->value<JudgeStruct*>()!=d->judge||!d->judge||d->judgeWho.isNull()
   ||d->judge->who!=d->judgeWho||d->judge->card!=d->judgeCard
   ||d->judge->isGood()!=d->judgeResult
   ||d->judge->reason!=d->reason||d->judge->pattern!=d->pattern||d->judge->good!=d->good
   ||!d->judgeLease||d->judgeLease()!=d->judgeCard
   ||r.roomState()->getCurrentCardUseReason()!=q.reason||r.roomState()->getCurrentCardUsePattern()!=q.pattern)return false;
  return true;
 }
 static bool material(Room&r,const AIRequest&q,ServerPlayer*p,AIRound47FengyiQuestion::Data*d,int id){
  if(!current(r,q,p,d)||id<0||!p->hasSkill("sijyu_fengyi")||p->getPile("sijyu_xing").isEmpty())return false;
  bool disclosed=false;for(const auto &c:q.worldView.handCards)if(c.effectiveId==id)disclosed=true;
  for(const auto &pile:q.worldView.self.piles)if(pile.name=="sijyu_xing"&&pile.cardIds.contains(id))disclosed=true;
  if(!disclosed)return false;
  if(r.getMode().contains("_mini_46")&&!d->judge->isGood()){
   const auto hand=p->handCards();if(hand.isEmpty()||hand.first()!=id||r.getCardOwner(id)!=p||r.getCardPlace(id)!=Player::PlaceHand)return false;
  }else if(!p->getPile("sijyu_xing").contains(id)||r.getCardOwner(id)!=p||r.getCardPlace(id)!=Player::PlaceSpecial)return false;
  const Card*c=Sanguosha->getCard(id);
  return c&&!c->isVirtualCard()&&c->getEffectiveId()==id&&!p->isCardLimited(c,Card::MethodResponse)&&current(r,q,p,d);
 }
};
AIRound47FengyiQuestion::AIRound47FengyiQuestion(Room&r,ServerPlayer*p,const QString&pattern,const QString&prompt,const QVariant&data,int method,bool retrial):d(new Data){
 d->room=&r;d->owner=p;d->data=&data;d->prompt=prompt;
 // Any nested native askForCard permanently revokes suspended authority, even
 // if it returns without changing the room revision or request certificate.
 for(auto *outer:frames.value(&r))outer->d->valid=false;
 frames[&r].append(this);
 auto *runtime=r.roomRuntime();
 if(!runtime||runtime->isClosing())return;
 const auto &game=runtime->lua();const auto &isolated=runtime->ai().lua();
 if(game.isClosing()||game.isClosed()||isolated.isClosing()||isolated.isClosed()
  ||!game.rawState()||!isolated.rawState())return;
 d->gameState=game.rawState();d->isolatedState=isolated.rawState();
 d->gameGeneration=game.generation();d->isolatedGeneration=isolated.generation();
 if(!p||p->getRoom()!=&r||!retrial||method!=Card::MethodResponse||pattern!="@@sijyu_fengyi"||!data.canConvert<JudgeStruct*>())return;
 auto*j=data.value<JudgeStruct*>();if(!j||!j->who||j->who->getRoom()!=&r||!j->card)return;
 if(prompt!="@sijyu_fengyi-card:"+j->who->objectName()+":sijyu_fengyi:"+j->reason)return;
 d->judge=j;d->judgeWho=j->who;d->judgeCard=j->card;d->reason=j->reason;d->pattern=j->pattern;d->good=j->good;d->judgeResult=j->isGood();
 d->judgeLease=AIOwnerReadonlyCardStore::observe(j->card);d->valid=bool(d->judgeLease)&&d->judgeLease()==j->card;
}
AIRound47FengyiQuestion::~AIRound47FengyiQuestion(){auto i=frames.find(d->room);if(i!=frames.end()){i->removeAll(this);if(i->isEmpty())frames.erase(i);}}
QJsonObject selectAIRound47FengyiPhysical(Room&r,const AIRequest&q,ServerPlayer*p,int id){
 auto*d=AIRound47FengyiAccess::top(r);
 const auto fail=[](){return QJsonObject{{"error","Fengyi physical answer lacks current native retrial authority"},{"native_rejected",true}};};
 if(!AIRound47FengyiAccess::current(r,q,p,d)||d->attempted)return fail();
 d->attempted=true; // Selected failure is terminal; no new material scan/ticket.
 if(!AIRound47FengyiAccess::material(r,q,p,d,id))return fail();
 auto lease=AIOwnerReadonlyCardStore::observe(Sanguosha->getCard(id));
 if(!lease||!AIRound47FengyiAccess::current(r,q,p,d))return fail();
 d->selected=id;d->decision=q.decisionId;d->revision=q.stateRevision;d->selectedLease=std::move(lease);
 return{{"accepted",true},{"card_id",id}};
}
const Card *consumeAIRound47FengyiPhysical(Room&r,const AIRequest&q,ServerPlayer*p,int id){
 auto*d=AIRound47FengyiAccess::top(r);
 if(!d||d->consumed||d->selected!=id||d->decision!=q.decisionId||d->revision!=q.stateRevision
  ||!d->selectedLease||!AIRound47FengyiAccess::material(r,q,p,d,id))return nullptr;
 const Card*c=d->selectedLease();if(!c||c!=Sanguosha->getCard(id)||!AIRound47FengyiAccess::current(r,q,p,d))return nullptr;
 d->consumed=true;return c;
}

#include "ai-strategy-round50-effects.h"
#include "ai-strategy-room-ledger.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-card-identity.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <QPointer>
#include <QSet>
#include <QWeakPointer>
#include <limits>
namespace {
struct Sequence final:AIRoomLedger {
 explicit Sequence(Room&r):AIRoomLedger(r){}
 const AIRequest*request=nullptr;quint64 decision=0;QWeakPointer<AIConversionLedger>ledger;QPointer<ServerPlayer>owner;
 bool written=false;QMap<QString,bool>flagReads;
};
Sequence*sequence(Room&r,const AIRequest&q,ServerPlayer*p){
 Sequence*s=nullptr;for(auto*c:r.children())if((s=dynamic_cast<Sequence*>(c)))break;
 if(!s)s=new Sequence(r);
 if(s->request!=&q||s->decision!=q.decisionId||s->owner!=p||s->ledger.toStrongRef()!=q.conversionLedger){
  s->request=&q;s->decision=q.decisionId;s->owner=p;s->ledger=q.conversionLedger;s->written=false;s->flagReads.clear();
 }return s;
}
QJsonObject fail(const char*s){return{{"known",false},{"reason",QString::fromLatin1(s)}};}
bool current(Room&r,const AIRequest&q,ServerPlayer*p){
 return p&&p->getRoom()==&r&&p->objectName()==q.viewerObjectName&&q.worldView.self.objectName==q.viewerObjectName
  &&q.conversionLedger&&q.decisionId&&aiRound44RangeRequestCurrent(r,q,p);
}
bool own(ServerPlayer*p,const QString&skill){for(const auto&i:p->getSkillInstances())if(i.skillName==skill)return true;return false;}
bool activate(const AIRequest&q){return q.kind==AIRequest::Activate&&q.reason==CardUseStruct::CARD_USE_REASON_PLAY;}
const Card*material(Room&r,const AIRequest&q,ServerPlayer*p,int id,bool handOnly){
 if(id<0||r.getCardOwner(id)!=p)return nullptr;
 const auto place=r.getCardPlace(id);if(place!=Player::PlaceHand&&(handOnly||place!=Player::PlaceEquip))return nullptr;
 const auto check=[&](const QList<AICardView>&rows)->const Card*{for(const auto&v:rows)if(v.effectiveId==id){
  const auto*c=r.getCard(id);if(c&&c->getEffectiveId()==id&&v.objectName==c->objectName()&&v.className==c->getClassName()
    &&v.suit==int(c->getSuit())&&v.number==c->getNumber()&&!v.legacyIdentityToken.isEmpty()
    &&v.legacyIdentityToken==aiLegacyCardIdentity(r,p,c))return c;
 }return nullptr;};
 return place==Player::PlaceHand?check(q.worldView.handCards):check(q.worldView.self.equips);
}
}
QJsonObject queryAIRound50Effects(Room&r,const AIRequest&q,ServerPlayer*p,const QJsonObject&input){
 if(!current(r,q,p))return fail("source effect requires exact current owner question");
 const QSet<QString>fields{"source","op","key","target","subcards"};
 if(input.size()!=fields.size())return fail("source effect schema differs");
 for(auto it=input.begin();it!=input.end();++it)if(!fields.contains(it.key()))return fail("unexpected source effect field");
 for(const auto&k:{"source","op","key","target"})if(!input.value(k).isString())return fail("source effect strings missing");
 if(!input.value("subcards").isArray())return fail("source effect materials missing");
 const auto source=input.value("source").toString(),op=input.value("op").toString(),key=input.value("key").toString(),name=input.value("target").toString();
 const auto ids=input.value("subcards").toArray();
 const QMap<QString,Card::Suit>suits{{"AI_has_clubs",Card::Club},{"AI_has_spades",Card::Spade},{"AI_has_diamonds",Card::Diamond},{"AI_has_hearts",Card::Heart}};
 bool mark=false,value=false;ServerPlayer*target=p;
 if(source=="heapridge"){
  if(q.kind!=AIRequest::UseCard||q.pattern!="@@heapridge"||q.prompt!="@heapridge"
   ||q.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE||!suits.contains(key)||name!=p->objectName())return fail("heapridge source question differs");
  mark=true;
  if(op=="read"){
   if(!ids.isEmpty())return fail("heapridge mark read has materials");
   const int n=p->getMark(key);if(!current(r,q,p))return fail("heapridge current changed during read");return{{"known",true},{"value",n}};
  }
  if(op!="write"||ids.size()!=4)return fail("heapridge reached source add requires four costs");
  QSet<int>seen;for(const auto&v:ids){const int id=v.toInt(-1);if(!v.isDouble()||v.toDouble()!=id||seen.contains(id))return fail("heapridge costs malformed");seen.insert(id);
   const Card*c=material(r,q,p,id,true);if(!c||c->getSuit()!=suits.value(key))return fail("heapridge source costs not current owner suit");}
 }else if(source=="anzhi"){
  if(key!="ai_anzhi-PlayClear"||name!=p->objectName()||!ids.isEmpty())return fail("anzhi mark schema differs");
  mark=true;
  if(op=="read"&&activate(q)&&own(p,"anzhi")){
   const int n=p->getMark(key);if(!current(r,q,p))return fail("anzhi read changed current");return{{"known",true},{"value",n}};
  }
  if(op!="write"||q.kind!=AIRequest::PlayerChosen||q.choiceOptions.reason!="anzhi")return fail("anzhi reached source add requires actual player choice");
 }else if(source=="maji"){
  if(q.kind!=AIRequest::RespondCard||q.choiceOptions.question!="askForCard"||q.pattern!=".."||q.prompt.section(':',0,0)!="@goddess-put"
   ||q.handlingMethod!=Card::MethodNone||q.worldView.decisionContext.value("judge").toObject().value("reason")!=QJsonValue("miyadokyo")
   ||name!=p->objectName()||(key!="majiBad"&&key!="majiGood"&&key!="majiHeal"))return fail("maji clear outside exact owner judge inquiry");
  if(op=="read"){
   if(!ids.isEmpty())return fail("maji read has material");const bool yes=p->getFlagList().contains(key);
   if(!current(r,q,p))return fail("maji read changed current");sequence(r,q,p)->flagReads.insert(key,yes);return{{"known",true},{"value",yes}};
  }
  if(op!="write"||ids.size()!=1||!sequence(r,q,p)->flagReads.value(key,false))return fail("maji clear has no reached positive flag read");
  const int id=ids.first().toInt(-1);if(!ids.first().isDouble()||ids.first().toDouble()!=id)return fail("maji material malformed");
  const Card*c=material(r,q,p,id,false);if(!c||(key=="majiBad"?!c->isRed():!c->isBlack())
   ||(key=="majiHeal"&&p->getLostHp()<=1))return fail("maji source color or heal condition differs");
 }else if(source=="turned"){
  if(!activate(q)||!own(p,"turned")||op!="write"||key!="turned_target"||!ids.isEmpty())return fail("turned source write outside current Play");
  target=r.findPlayerByObjectName(name);value=true;
  if(!target||!target->isAlive()||!q.worldView.playerOrder.contains(name))return fail("turned target is not live public roster");
 }else return fail("source effect is outside finite catalog");
 if(!current(r,q,p))return fail("source effect current changed before write");
 auto*s=sequence(r,q,p);if(s->written)return fail("source effect already reached; no duplicate or reselect");
 s->written=true; // Mark the reached prefix before native signals can reenter.
 AIRound50EffectPermit permit(r,q,p,target,key,mark,value);QString error;
 if(!writeAIRound50Effect(r,q,p,target,permit,&error))return{{"known",false},{"reason",error}};
 return{{"known",true}};
}

#include "ai-strategy-round47-xiedou.h"
#include "ai-strategy-room-ledger.h"
#include "ai.h"
#include "ai-strategy-round44-certificate.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <QSet>
#include <cmath>
#include <limits>
bool aiRound47XiedouConstruction(const AIRequest &r,const ServerPlayer *p,const QJsonObject &s)
{
 if(!p||!p->getRoom()||r.viewerObjectName!=p->objectName()
  ||!aiRound44RangeRequestCurrent(*p->getRoom(),r,p)
  ||r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY
  ||s.size()!=6||s.value("source_key")!=QJsonValue("xiedou")
  ||s.value("name")!=QJsonValue("duel")||s.value("skill")!=QJsonValue("xiedou")
  ||!s.value("subcards").isArray()||!p->hasSkill("xiedou"))return false;
 const auto ids=s.value("subcards").toArray();
 const int equips=p->getEquips().size(),count=p->getHandcardNum()-equips;
 if(equips==0||count<=0||ids.size()!=count)return false;
 QSet<int> seen;const Card *first=nullptr;
 for(const auto &value:ids){
  const double n=value.toDouble(-1);
  if(!value.isDouble()||!std::isfinite(n)||n<0||n>std::numeric_limits<int>::max()||n!=std::floor(n))return false;
  const int id=int(n);bool disclosed=false;
  for(const auto &card:r.worldView.handCards)if(card.effectiveId==id){disclosed=true;break;}
  if(!disclosed||seen.contains(id)||!p->handCards().contains(id)||p->getRoom()->getCardOwner(id)!=p)return false;
  seen.insert(id);const Card *material=Sanguosha->getCard(id);if(!material)return false;
  if(!first)first=material;
 }
 // This is the reached readonly Card_Parse, NOT native payment. In particular
 // using/jilei/quota/native target admission must not reorder source selection.
 const int number=first->getNumber()>13?0:first->getNumber();
 return s.value("suit")==QJsonValue(int(first->getSuit()))&&s.value("number")==QJsonValue(number)
  &&aiRound44RangeRequestCurrent(*p->getRoom(),r,p);
}


#include "card-lifetime-manager.h"
#include <QObject>
#include <QPointer>
#include <QHash>
namespace {
struct XiedouFlagEntry {
 std::weak_ptr<const CardLifetimeToken> token;
 QPointer<const Card> card;
 quint64 epoch=0;
};
class XiedouFlagLedger final:public AIRoomLedger {
public:
 explicit XiedouFlagLedger(Room &room):AIRoomLedger(room){}
 QHash<const Card*,XiedouFlagEntry> entries;
};
XiedouFlagLedger *xiedouFlagLedger(Room &room,bool create)
{
 for(QObject *child:room.children())if(auto *l=dynamic_cast<XiedouFlagLedger*>(child))return l;
 return create?new XiedouFlagLedger(room):nullptr;
}
}
class AIRound47XiedouObservation {
public:
 static void invalidate(const Card *card) noexcept
 {
  if(card && card->m_aiXiedouNosJiefanEpoch != ~quint64(0))
   ++card->m_aiXiedouNosJiefanEpoch;
 }
 static void record(Room &room,const Card *card,bool xufaValue=false)
 {
  if(!card||!card->isVirtualCard()||card->getClassName()!=QStringLiteral("Duel")
    ||card->hasFlag("nosjiefan-slash"))return;
  if(xufaValue){
   // Only the fresh zero/one-material Duel parsed by the actual Xufa value
   // continuation calls this issuer. A class/skill match alone grants nothing.
   if(card->getSkillName(false)!=QStringLiteral("xufa")
     ||card->getSubcards().size()>1
     ||(!card->getSubcards().isEmpty()&&card->getSubcards().first()<0))return;
  }else if(!card->getSubcards().isEmpty()||card->getSkillName(false)!=QStringLiteral("xiedou")
    ||card->getSuit()!=Card::NoSuit||card->getNumber()!=0)return;
  const auto token=globalCardLifetimeManager().liveToken(card);
  if(!token||!token->live||card->m_aiXiedouNosJiefanEpoch==~quint64(0))return;
  auto *l=xiedouFlagLedger(room,true);
  for(auto i=l->entries.begin();i!=l->entries.end();) {
   const auto old=i->token.lock();if(!old||!old->live||i->card.isNull())i=l->entries.erase(i);else ++i;
  }
  // Dead-address reuse may issue a new object; the same live object never revives.
  if(l->entries.contains(card))return;
  l->entries.insert(card,{token,card,card->m_aiXiedouNosJiefanEpoch});
 }
 static QJsonObject read(Room &room,ServerPlayer *viewer,const Card *card)
 {
  if(!viewer||viewer->getRoom()!=&room||!card)return{};
  auto *l=xiedouFlagLedger(room,false);if(!l)return{};
  const auto i=l->entries.constFind(card);if(i==l->entries.cend()||i->card.isNull()||i->card.data()!=card)return{};
  const auto token=i->token.lock();
  if(!token||!token->live||globalCardLifetimeManager().liveToken(card)!=token
    ||card->m_aiXiedouNosJiefanEpoch==~quint64(0)
    ||i->epoch!=card->m_aiXiedouNosJiefanEpoch)return{};
  return{{QStringLiteral("nosjiefan-slash"),false}};
 }
};
void recordAIRound47FreshXiedouDuel(Room &room,const Card *card)
{AIRound47XiedouObservation::record(room,card);}
void recordAIRound53FreshXufaValueDuel(Room &room,const Card *card)
{AIRound47XiedouObservation::record(room,card,true);}
void invalidateAIRound47XiedouFlags(const Card *card) noexcept
{AIRound47XiedouObservation::invalidate(card);}
QJsonObject readAIRound47XiedouFlags(Room &room,ServerPlayer *viewer,const Card *card)
{return AIRound47XiedouObservation::read(room,viewer,card);}

#include "ai-strategy-round43-planning.h"
#include "ai-strategy-card-identity.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <QSet>
#include <cmath>
#include <limits>
namespace {
bool same(const AICardView&v,const Card*c,int id,Room&room,const ServerPlayer*p){
 return c&&v.effectiveId==id&&v.objectName==c->objectName()&&v.className==c->getClassName()
  &&v.suit==int(c->getSuit())&&v.number==c->getNumber()&&!v.legacyIdentityToken.isEmpty()
  &&v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c);
}
bool material(const AIRequest&r,Room&room,const ServerPlayer*p,int id){
 if(room.getCardOwner(id)!=p)return false;
 const Card*c=Sanguosha->getCard(id);if(!c)return false;
 if(room.getCardPlace(id)==Player::PlaceHand&&p->handCards().contains(id))
  for(const auto&v:r.worldView.handCards)if(same(v,c,id,room,p))return true;
 // Fixed addHandPile = current owner hand plus wooden_ox / &-prefixed piles.
 // Native selection remains independently checked after the policy selects.
 if(room.getCardPlace(id)!=Player::PlaceSpecial||!p->getHandPile().contains(id))return false;
 for(const auto&pile:r.worldView.self.piles)
  if((pile.name=="wooden_ox"||pile.name.startsWith('&'))&&p->getPile(pile.name).contains(id))
   for(const auto&v:pile.cards)if(same(v,c,id,room,p))return true;
 return false;
}
}
bool aiRound43PlanningConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 static const QSet<QString>keys{"source_key","name","skill","suit","number","subcards"};
 if(!p||p->getRoom()!=&room||room.roomRuntime()->isClosing()||r.decisionId==0
  ||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.stateRevision!=room.roomRuntime()->stateRevision()||r.kind!=AIRequest::Activate
  ||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!r.pattern.isEmpty()||!p->hasSkill("mouluanji")
  ||s.size()!=keys.size())return false;
 for(auto it=s.begin();it!=s.end();++it)if(!keys.contains(it.key()))return false;
 if(s.value("source_key")!=QJsonValue("newgenerals.mouluanji")||s.value("name")!=QJsonValue("archery_attack")
  ||s.value("skill")!=QJsonValue("mouluanji")||s.value("suit")!=QJsonValue(int(Card::NoSuit))
  ||s.value("number")!=QJsonValue(0)||!s.value("subcards").isArray())return false;
 const auto ids=s.value("subcards").toArray();if(ids.size()!=2)return false;
 QSet<int>seen;for(const auto&v:ids){
  const double n=v.toDouble(-1);if(!v.isDouble()||!std::isfinite(n)||n<0||n>std::numeric_limits<int>::max()||n!=std::floor(n))return false;
  const int id=int(n);if(seen.contains(id)||!material(r,room,p,id))return false;seen.insert(id);
 }
 // No mark/availability/payment check here: this is only the source's ordered
 // hypothetical clone. The original fill owns its early mark/search branches;
 // selected-only native canActivate/canSelect/createCard remain authoritative.
 return true;
}

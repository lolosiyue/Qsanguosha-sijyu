#include "ai-strategy-round49-planning.h"
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
#include <cmath>
#include <limits>
namespace {
bool exact(const AICardView&v,const Card*c,Room&room,const ServerPlayer*p,int id){
 return c&&v.effectiveId==id&&c->getEffectiveId()==id&&v.objectName==c->objectName()
  &&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber()
  &&!v.legacyIdentityToken.isEmpty()&&v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c);
}
}
bool aiRound49PlanningConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!r.pattern.isEmpty()
  ||!aiRound44RangeRequestCurrent(room,r,p))return false;
 const QSet<QString>fields{"source_key","skill","name","suit","number","subcards"};
 if(s.size()!=fields.size())return false;for(auto i=s.begin();i!=s.end();++i)if(!fields.contains(i.key()))return false;
 if(s.value("source_key")!=QJsonValue("newgenerals.keolshuangxiong")||s.value("skill")!=QJsonValue("keolshuangxiong")
  ||s.value("name")!=QJsonValue("duel")||s.value("suit")!=QJsonValue(int(Card::NoSuit))||s.value("number")!=QJsonValue(0)
  ||!s.value("subcards").isArray()||s.value("subcards").toArray().size()!=1)return false;
 // Readonly source presence, not canActivate/invalidity/color/using admission.
 // The source fill observes a proposal before selected native V2 guards.
 bool own=false;for(const auto&instance:p->getSkillInstances())if(instance.skillName=="keolshuangxiong")own=true;
 if(!own)return false;
 const auto v=s.value("subcards").toArray().first();const double n=v.toDouble(-1);
 if(!v.isDouble()||!std::isfinite(n)||n<0||n!=std::floor(n)||n>std::numeric_limits<int>::max())return false;
 const int id=int(n);if(room.getCardOwner(id)!=p)return false;
 if(room.getCardPlace(id)==Player::PlaceHand&&p->handCards().contains(id)){
  const Card*c=Sanguosha->getCard(id);for(const auto&face:r.worldView.handCards)if(exact(face,c,room,p,id))return true;
 }
 // getCards("he") returns actual inner Equip objects. This narrowly proves
 // the current owner's source object; no wrapper/foreign/pile substitution.
 if(room.getCardPlace(id)==Player::PlaceEquip)for(const Card*c:p->getEquips())if(c->getEffectiveId()==id)
  for(const auto&face:r.worldView.self.equips)if(exact(face,c,room,p,id))return true;
 return false;
}

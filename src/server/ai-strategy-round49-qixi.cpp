#include "ai-strategy-round49-qixi.h"
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
bool aiRound49QixiConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()||!r.decisionId
  ||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!r.pattern.isEmpty()
  ||!aiRound44RangeRequestCurrent(room,r,p))return false;
 const QSet<QString>fields{"source_key","skill","name","suit","number","subcards"};
 if(s.size()!=fields.size())return false;for(auto i=s.begin();i!=s.end();++i)if(!fields.contains(i.key()))return false;
 const QString key=s.value("source_key").toString(),name=s.value("name").toString();
 if(key!="qhstandardqixi"||name!="dismantlement"||s.value("skill")!=QJsonValue(key)
  ||!s.value("name").isString()||!s.value("suit").isDouble()||!s.value("number").isDouble()
  ||!s.value("subcards").isArray()||s.value("subcards").toArray().size()!=1)return false;
 bool owned=false;for(const auto&i:p->getSkillInstances())if(i.skillName==key)owned=true;if(!owned)return false;
 const auto v=s.value("subcards").toArray().first();const double n=v.toDouble(-1);
 if(!v.isDouble()||!std::isfinite(n)||n<0||n!=std::floor(n)||n>std::numeric_limits<int>::max())return false;
 const int id=int(n);if(room.getCardOwner(id)!=p)return false;
 const Card*c=nullptr;const AICardView*face=nullptr;
 if(room.getCardPlace(id)==Player::PlaceHand&&p->handCards().contains(id)){
  c=Sanguosha->getCard(id);for(const auto&row:r.worldView.handCards)if(row.effectiveId==id){face=&row;break;}
 }else if(room.getCardPlace(id)==Player::PlaceEquip){
  for(const Card*e:p->getEquips())if(e->getEffectiveId()==id){c=e;break;}
  for(const auto&row:r.worldView.self.equips)if(row.effectiveId==id){face=&row;break;}
 }
 if(!c||!face||face->objectName!=c->objectName()||face->className!=c->getClassName()
  ||face->suit!=int(c->getSuit())||face->number!=c->getNumber()||face->legacyIdentityToken.isEmpty()
  ||face->legacyIdentityToken!=aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c)
  ||s.value("suit")!=QJsonValue(int(Card::NoSuit))||s.value("number")!=QJsonValue(0))return false;
 // The source constructs for EVERY HE card before the black short circuit.
 // Source NoSuit/0 is a constructor tuple, not its inherited material face.
 // This capability evaluates only; selected native skill/cost gates are unchanged.
 return true;
}

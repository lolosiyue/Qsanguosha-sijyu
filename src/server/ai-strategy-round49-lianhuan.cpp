#include "ai-strategy-round49-lianhuan.h"
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
bool aiRound49LianhuanConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s)
{
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!r.pattern.isEmpty()
  ||!aiRound44RangeRequestCurrent(room,r,p))return false;
 const QSet<QString>fields{"source_key","skill","name","suit","number","subcards"};
 if(s.size()!=fields.size())return false;for(auto i=s.begin();i!=s.end();++i)if(!fields.contains(i.key()))return false;
 if(s.value("source_key")!=QJsonValue("qhfirelianhuan")||s.value("skill")!=QJsonValue(QString())
  ||s.value("name")!=QJsonValue("thunder_slash")||s.value("suit")!=QJsonValue(int(Card::Spade))
  ||!s.value("number").isDouble()||!s.value("subcards").isArray()||!s.value("subcards").toArray().isEmpty())return false;
 // Fixed qhstandard-ai.lua1207–1237: a fresh unskilled, cost-free
 // thunder Slash carries the printed number of an actual own HE spade.
 // Presence attests this source; selected provider eligibility stays separate.
 bool owned=false;for(const auto&i:p->getSkillInstances())if(i.skillName=="qhfirelianhuan")owned=true;
 if(!owned)return false;
 const auto exact=[&](const AICardView&v,const Card*c){
  if(!c||c->getEffectiveId()<0||c->getSuit()!=Card::Spade||s.value("number")!=QJsonValue(c->getNumber()))return false;
  const int id=c->getEffectiveId();return room.getCardOwner(id)==p&&v.effectiveId==id
   &&v.objectName==c->objectName()&&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber()
   &&!v.legacyIdentityToken.isEmpty()&&v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c);
 };
 for(int id:p->handCards())if(room.getCardPlace(id)==Player::PlaceHand)
  for(const auto&face:r.worldView.handCards)if(face.effectiveId==id&&exact(face,Sanguosha->getCard(id)))return true;
 // Player::getCards("he") supplies the actual equipment object, not a
 // replacement wrapper. Require that exact already-disclosed source identity.
 for(const Card*c:p->getEquips())if(c&&room.getCardPlace(c->getEffectiveId())==Player::PlaceEquip)
  for(const auto&face:r.worldView.self.equips)if(exact(face,c))return true;
 return false;
}

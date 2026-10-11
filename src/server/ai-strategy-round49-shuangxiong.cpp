#include "ai-strategy-round49-shuangxiong.h"
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
bool aiRound49ShuangxiongConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s)
{
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!r.pattern.isEmpty()
  ||!aiRound44RangeRequestCurrent(room,r,p))return false;
 const QSet<QString>fields{"source_key","skill","name","suit","number","subcards"};
 if(s.size()!=fields.size())return false;for(auto i=s.begin();i!=s.end();++i)if(!fields.contains(i.key()))return false;
 if(s.value("source_key")!=QJsonValue("qhfireshuangxiong")||s.value("skill")!=QJsonValue(QString())
  ||!(s.value("name")==QJsonValue("duel")||s.value("name")==QJsonValue("slash"))||!s.value("suit").isDouble()
  ||!s.value("number").isDouble()||!s.value("subcards").isArray()||!s.value("subcards").toArray().isEmpty())return false;
 // Fixed qhstandard-ai.lua3100–3147: an unskilled, cost-free Duel
 // or Slash carries the exact printed suit/number of an actual own hand card.
 // Presence attests this source; selected provider eligibility stays separate.
 bool owned=false;for(const auto&i:p->getSkillInstances())if(i.skillName=="qhfireshuangxiong")owned=true;
 if(!owned||p->getMark("&qhfireshuangxiong_damage-Clear")==2)return false;
 int sourceColor=-1;
 if(p->getMark("&qhfireshuangxiong_red-Clear")==1)sourceColor=int(Card::Red);
 else if(p->getMark("&qhfireshuangxiong_black-Clear")==1)sourceColor=int(Card::Black);
 const auto exact=[&](const AICardView&v,const Card*c){
  if(!c||c->getEffectiveId()<0||s.value("suit")!=QJsonValue(int(c->getSuit()))||s.value("number")!=QJsonValue(c->getNumber()))return false;
  const QString sourceName=int(c->getColor())==sourceColor?QStringLiteral("slash"):QStringLiteral("duel");
  if(s.value("name")!=QJsonValue(sourceName))return false;
  const int id=c->getEffectiveId();return room.getCardOwner(id)==p&&v.effectiveId==id
   &&v.objectName==c->objectName()&&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber()
   &&!v.legacyIdentityToken.isEmpty()&&v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c);
 };
 for(int id:p->handCards())if(room.getCardPlace(id)==Player::PlaceHand)
  for(const auto&face:r.worldView.handCards)if(face.effectiveId==id&&exact(face,Sanguosha->getCard(id)))return true;
 return false;
}

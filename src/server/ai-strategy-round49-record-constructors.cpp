#include "ai-strategy-round49-record-constructors.h"
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
bool aiRound49RecordConstruction(const AIRequest&q,Room&r,const ServerPlayer*p,const QJsonObject&s){
 if(!p||!r.getPlayers().contains(p)||p->getRoom()!=&r||!r.roomRuntime()||r.roomRuntime()->isClosing()||!q.decisionId||q.viewerObjectName!=p->objectName()||q.worldView.self.objectName!=p->objectName()||!aiRound44RangeRequestCurrent(r,q,p))return false;
 const QSet<QString> fields{"source_key","skill","name","suit","number","subcards"};if(s.size()!=fields.size())return false;for(auto it=s.begin();it!=s.end();++it)if(!fields.contains(it.key()))return false;
 if(!s.value("source_key").isString()||!s.value("skill").isString()||!s.value("name").isString()||s.value("suit")!=QJsonValue(int(Card::NoSuit))||s.value("number")!=QJsonValue(0)||!s.value("subcards").isArray())return false;
 const QString key=s.value("source_key").toString(),raw=s.value("skill").toString(),name=s.value("name").toString();const auto costs=s.value("subcards").toArray();QString property;
 if(key=="choulve"||key=="@@choulve!"){
  if(!((key=="choulve"&&q.kind==AIRequest::PlayerChosen&&q.choiceOptions.reason=="choulve"&&q.choiceOptions.question=="askForPlayerChosen")||(key=="@@choulve!"&&q.kind==AIRequest::UseCard&&q.pattern==key&&q.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE)))return false;
  return costs.isEmpty()&&(raw.isEmpty()||raw=="_choulve")&&name==p->property("choulve_damage_card").toString();
 }
 if(q.kind!=AIRequest::Activate||q.reason!=CardUseStruct::CARD_USE_REASON_PLAY||!q.pattern.isEmpty())return false;
 if(key=="fengying"||key=="fuping")property="SkillDescriptionRecord_"+key;
 else if(key=="jingong"||key=="tenyearjingong")property=key+"_tricks";else return false;
 if(!p->property(property.toUtf8().constData()).toString().split('+').contains(name)||(raw!=key&&!raw.isEmpty())||costs.size()>1)return false;
 if(costs.isEmpty())return key=="fengying"||raw.isEmpty();
 const auto v=costs.first();const double d=v.toDouble(-1);if(!v.isDouble()||!std::isfinite(d)||d<0||d!=std::floor(d)||d>std::numeric_limits<int>::max())return false;
 const int id=int(d);if(r.getCardOwner(id)!=p)return false;const Card*c=r.getCard(id);if(!c)return false;const AICardView*face=nullptr;
 if(r.getCardPlace(id)==Player::PlaceHand&&p->handCards().contains(id)){for(const auto&x:q.worldView.handCards)if(x.effectiveId==id){face=&x;break;}}
 else if(key!="fengying"&&r.getCardPlace(id)==Player::PlaceEquip){for(const auto&x:q.worldView.self.equips)if(x.effectiveId==id){face=&x;break;}}
 else if((key=="jingong"||key=="tenyearjingong")&&p->getPile("wooden_ox").contains(id)){for(const auto&pile:q.worldView.self.piles)if(pile.name=="wooden_ox")for(const auto&x:pile.cards)if(x.effectiveId==id){face=&x;break;}}
 if(!face||face->objectName!=c->objectName()||face->className!=c->getClassName()||face->suit!=int(c->getSuit())||face->number!=c->getNumber()||face->legacyIdentityToken.isEmpty()||face->legacyIdentityToken!=aiLegacyCardIdentity(r,const_cast<ServerPlayer*>(p),c))return false;
 if(key=="fengying")return raw==key&&c->getNumber()<=p->getMark("&dgrlzjiao");
 if(key=="fuping")return c->getTypeId()!=Card::TypeBasic;
 return c->isKindOf("Slash")||c->isKindOf("EquipCard");
}

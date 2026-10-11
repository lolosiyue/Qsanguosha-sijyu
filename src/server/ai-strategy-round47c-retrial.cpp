#include "ai-strategy-round47c-retrial.h"
#include "ai-strategy-round44-certificate.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <cmath>
#include <limits>
// Fixed DevSaodongUse::onDamaged asks MethodNone with the owner HE card-ID
// pattern. These TWO source hypotheses precede selection; they issue no receipt.
bool aiRound47cSaodongConstruction(const AIRequest &r,const ServerPlayer *p,const QJsonObject &s)
{
 if(!p||!p->getRoom()||r.viewerObjectName!=p->objectName()
  ||!aiRound44RangeRequestCurrent(*p->getRoom(),r,p)
  ||r.kind!=AIRequest::RespondCard||r.choiceOptions.question!="askForCard"
  ||r.prompt!="@dev_saodong"||r.handlingMethod!=Card::MethodNone
  ||r.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE||!p->hasSkill("dev_saodong")
  ||s.size()!=6||s.value("source_key")!=QJsonValue("dev_saodong")
  ||s.value("skill")!=QJsonValue("dev_saodong")
  ||(s.value("name")!=QJsonValue("peach")&&s.value("name")!=QJsonValue("ex_nihilo"))
  ||s.value("suit")!=QJsonValue(int(Card::NoSuit))||s.value("number")!=QJsonValue(0)
  ||!s.value("subcards").isArray())return false;
 const auto ids=s.value("subcards").toArray();if(ids.size()!=1)return false;
 const double number=ids[0].toDouble(-1);
 if(!ids[0].isDouble()||!std::isfinite(number)||number<0
   ||number>std::numeric_limits<int>::max()||number!=std::floor(number))return false;
 const int id=int(number);bool disclosed=false;
 for(const AICardView &c:r.worldView.handCards)if(c.effectiveId==id)disclosed=true;
 for(const AICardView &c:r.worldView.self.equips)if(c.effectiveId==id)disclosed=true;
 const Room *room=p->getRoom();
 if(!disclosed||room->getCardOwner(id)!=p
   ||(room->getCardPlace(id)!=Player::PlaceHand&&room->getCardPlace(id)!=Player::PlaceEquip))return false;
 const Card *material=Sanguosha->getCard(id);
 // Do not filter by availability/pattern here: the fixed source constructs
 // both products for EVERY heart, before its weak/draw/availability branches.
 return material&&material->getSuit()==Card::Heart
   &&aiRound44RangeRequestCurrent(*p->getRoom(),r,p);
}

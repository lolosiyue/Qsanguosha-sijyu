#include "ai-strategy-round49e-response.h"
#include "ai.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-card-identity.h"
#include "engine.h"
#include "card.h"
#include "room.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <cmath>
#include <limits>
bool aiRound49ePanqinEvaluation(const AIRequest&r,const ServerPlayer*p,const QJsonObject&q)
{
 if(!p||!p->getRoom()||r.viewerObjectName!=p->objectName()
  ||!aiRound44RangeRequestCurrent(*p->getRoom(),r,p)
  ||r.kind!=AIRequest::SkillInvoke||r.choiceOptions.reason!="panqin"
  ||q.size()!=4||q.value("kind")!="evaluate_skill_card"||q.value("skill")!="panqin"
  ||q.value("name")!="SavageAssault"||!q.value("subcards").isArray())return false;
 const auto tag=r.choiceOptions.context.value("ol_sp").toObject().value("tags").toObject().value("PanqinRecord").toObject();
 if(tag.value("known")!=QJsonValue(true)||tag.value("owner")!="owner"||tag.value("kind")!="IntList"||!tag.value("value").isArray())return false;
 QJsonArray native;for(const QVariant &v:p->getTag("PanqinRecord").toList())native.append(v.toInt());
 const auto recorded=tag.value("value").toArray();if(recorded!=native)return false;
 QJsonArray expected;Room &room=*p->getRoom();
 for(const auto &v:recorded){
  if(!v.isDouble())return false;const double d=v.toDouble();
  if(!std::isfinite(d)||d<std::numeric_limits<int>::min()||d>std::numeric_limits<int>::max()||d!=std::floor(d))return false;
  const int id=int(d);const AICardView *face=nullptr;
  // Discard membership is already public in this exact snapshot. Never ask a
  // hidden current location to choose whether the proposal is acceptable.
  for(const auto &f:r.worldView.discardPile)if(f.effectiveId==id){face=&f;break;}
  if(!face)continue;
  if(room.getCardPlace(id)!=Player::DiscardPile)return false;
  const Card*c=Sanguosha->getCard(id);
  if(!c||face->objectName!=c->objectName()||face->className!=c->getClassName()
   ||face->suit!=int(c->getSuit())||face->number!=c->getNumber()
   ||face->legacyIdentityToken.isEmpty()||face->legacyIdentityToken!=aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c))return false;
  expected.append(id);
 }
 // Source addSubcard(int) preserves duplicates and recorded order. This is
 // readonly SavageAssault policy only, never a discard payment or receipt.
 return q.value("subcards").toArray()==expected&&p->hasSkill("panqin")&&aiRound44RangeRequestCurrent(room,r,p);
}

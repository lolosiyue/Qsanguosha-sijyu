#include "ai-strategy-round49d-response.h"
#include "ai.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-card-identity.h"
#include "card.h"
#include "room.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <QSet>
#include <cmath>
#include <limits>
bool aiRound49dXixiangEvaluation(const AIRequest &r,const ServerPlayer *p,const QJsonObject &q)
{
 if(!p||!p->getRoom()||r.viewerObjectName!=p->objectName()
  ||!aiRound44RangeRequestCurrent(*p->getRoom(),r,p)
  ||r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY
  ||q.size()!=4||q.value("kind")!="evaluate_skill_card"||q.value("skill")!="olspxixiang"
  ||(q.value("name")!="slash"&&q.value("name")!="duel")||!q.value("subcards").isArray())return false;
 const QJsonArray ids=q.value("subcards").toArray();if(ids.size()!=2)return false;
 Room &room=*p->getRoom();QSet<int> seen;
 for(const auto &v:ids){
  if(!v.isDouble())return false;const double d=v.toDouble();
  if(!std::isfinite(d)||d<0||d>std::numeric_limits<int>::max()||d!=std::floor(d)||seen.contains(int(d)))return false;
  const int id=int(d);seen.insert(id);if(room.getCardOwner(id)!=p)return false;
  const Card *material=nullptr;
  const auto same=[&](const AICardView &f,const Card *c){return f.effectiveId==id&&f.objectName==c->objectName()
   &&f.className==c->getClassName()&&f.suit==int(c->getSuit())&&f.number==c->getNumber()
   &&!f.legacyIdentityToken.isEmpty()&&f.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c);};
  for(const Card *c:p->getCards("he"))if(c&&c->getEffectiveId()==id){
   if(room.getCardPlace(id)==Player::PlaceHand){for(const auto &f:r.worldView.handCards)if(same(f,c))material=c;}
   else if(room.getCardPlace(id)==Player::PlaceEquip){for(const auto &f:r.worldView.self.equips)if(same(f,c))material=c;}
  }
  if(!material)return false;
 }
 // Fixed source builds its two-cost hypothesis BEFORE mark/availability reads.
 // No canSelect/feasible/payment here; selected existing V2 owns that admission.
 return p->hasSkill("olspxixiang")&&aiRound44RangeRequestCurrent(room,r,p);
}

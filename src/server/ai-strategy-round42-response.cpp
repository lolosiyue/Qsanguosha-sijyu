#include "ai-strategy-round42-response.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
namespace {
bool current(const AIRequest&r,const ServerPlayer*p){
 return p&&p->getRoom()&&r.kind==AIRequest::Activate
  &&r.reason==CardUseStruct::CARD_USE_REASON_PLAY&&r.pattern.isEmpty()
  &&r.viewerObjectName==p->objectName()&&p->getPhase()==Player::Play
  &&p->getRoom()->getCurrent()==p&&p->hasSkill("limu")
  &&p->getRoom()->roomRuntime()->stateRevision()==r.stateRevision;
}
bool disclosed(const AIRequest&r,int id){
 for(const auto&v:r.worldView.handCards)if(v.effectiveId==id)return true;
 for(const auto&v:r.worldView.self.equips)if(v.effectiveId==id)return true;
 for(const auto&pile:r.worldView.self.piles)for(const auto&v:pile.cards)if(v.effectiveId==id)return true;
 return false;
}
}
int aiRound42ResponsePrototype(const AIRequest&r,const ServerPlayer*p,const QString&skill,const QString&name){
 return current(r,p)&&skill=="limu"&&name=="LimuCard"?1:-1;
}
std::unique_ptr<Card> aiRound42LimuEvaluation(const AIRequest&r,Room&room,ServerPlayer*p,
 const QString&kind,const QList<int>&costs,QString*error){
 auto fail=[&](const char*m)->std::unique_ptr<Card>{if(error)*error=m;return {};};
 if(!current(r,p)||p->getRoom()!=&room)return fail("Limu source hypothesis requires actual current owner Play");
 if((kind=="indulgence"&&costs.size()!=1)||(kind=="spear"&&costs.size()!=2)
   ||(kind!="indulgence"&&kind!="spear"))return fail("Limu source hypothesis shape changed");
 QList<int> seen;
 for(int id:costs){
  if(id<0||seen.contains(id)||!disclosed(r,id)||room.getCardOwner(id)!=p)return fail("Limu source material is not distinct disclosed owner material");
  if(!p->handCards().contains(id)&&(kind!="indulgence"||!p->getHandPile().contains(id)))return fail("Limu source material is outside exact source zone");
  seen<<id;
 }
 if(kind=="indulgence"&&Sanguosha->getCard(costs.first())->getSuit()!=Card::Diamond)return fail("Limu lock hypothesis requires source Diamond material");
 if(kind=="spear"&&!p->hasWeapon("spear"))return fail("Limu local Spear hypothesis requires equipped Spear");
 std::unique_ptr<Card> card(Sanguosha->cloneCard(kind=="indulgence"?"indulgence":"slash",Card::NoSuit,0));
 if(!card)return fail("fixed Limu source printed prototype is unavailable");
 if(kind=="indulgence"){card->addSubcards(costs);card->setSkillName("limu");}
 else{card->setSkillName("spear");card->addSubcards(costs);}
 if(error)error->clear();return card;
}

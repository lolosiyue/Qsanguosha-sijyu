#include "ai-strategy-round49c-planning.h"
#include "ai-strategy-card-identity.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonArray>
namespace {
bool same(const AICardView&v,const Card*c,Room&room,const ServerPlayer*p,int id){
 return c&&v.effectiveId==id&&v.objectName==c->objectName()&&v.className==c->getClassName()
  &&v.suit==int(c->getSuit())&&v.number==c->getNumber()&&!v.legacyIdentityToken.isEmpty()
  &&v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c);
}
bool disclosed(const AIWorldView&w,Room&room,const ServerPlayer*p,int id){
 const Card*c=Sanguosha->getCard(id);if(!c)return false;
 for(const auto&v:w.handCards)if(same(v,c,room,p,id))return true;
 for(const auto&v:w.discardPile)if(same(v,c,room,p,id))return true;
 auto players=w.players;players.prepend(w.self);
 for(const auto&v:players){
  for(const auto&k:v.knownCards)if(same(k,c,room,p,id))return true;
  for(const auto&k:v.equips)if(same(k,c,room,p,id))return true;
  for(const auto&k:v.judgingArea)if(same(k,c,room,p,id))return true;
  for(const auto&pile:v.piles)if(v.objectName==p->objectName()||pile.open)
   for(const auto&k:pile.cards)if(same(k,c,room,p,id))return true;
 }
 return false;
}
}
void projectAIRound49cPlanning(AIRequest&r,Room&room,const ServerPlayer*p){
 auto cleared=r.worldView.strategyFacts.value("shixinrumo_yi").toObject();
 cleared.remove("yikuxinIds");cleared.remove("chenlanjiaoIds");
 r.worldView.strategyFacts.insert("shixinrumo_yi",cleared);
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.stateRevision!=room.roomRuntime()->stateRevision())return;
 QString key;
 if(r.kind==AIRequest::PlayerChosen&&r.choiceOptions.question=="askForPlayerChosen"&&r.choiceOptions.reason=="yikuxin")key="yikuxinIds";
 else if(r.kind==AIRequest::SkillInvoke&&r.choiceOptions.question=="askForSkillInvoke"&&r.choiceOptions.reason=="chenlanjiao")key="chenlanjiaoIds";
 else return;
 // Fixed QVariant::toIntList absent/wrong-type result is the known empty list.
 // A nonempty list is atomic: no partial foreign-ID projection is permitted.
 const auto ids=p->getTag(key).value<QList<int>>();QJsonArray out;
 for(int id:ids){
  if(id<0)return;
  if(key=="yikuxinIds") {if(!disclosed(r.worldView,room,p,id))return;}
  else {
   // Native chenlanjiao asks the recipient, not the skill owner. No hasSkill
   // gate. Its source only tests membership in the recipient's current HE.
   bool owned=false;for(const Card*c:p->getCards("he"))if(c->getEffectiveId()==id)owned=true;
   if(!owned||room.getCardOwner(id)!=p)return;
  }
  out.append(id); // Preserve source order and duplicates, including empty.
 }
 auto facts=r.worldView.strategyFacts.value("shixinrumo_yi").toObject();facts.insert(key,out);
 r.worldView.strategyFacts.insert("shixinrumo_yi",facts);
}

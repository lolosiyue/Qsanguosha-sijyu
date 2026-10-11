#include "ai-strategy-round49-yitian.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "general.h"
#include <QJsonObject>
void projectAIRound49Yitian(AIRequest &q,Room &r,ServerPlayer *p){
 q.choiceOptions.context.remove("guixin_modify_target");q.worldView.decisionContext.remove("guixin_modify_target");
 auto y=q.worldView.strategyFacts.value("yitian").toObject();y.remove("general_is_male");
 const auto publish=[&]{if(y.isEmpty())q.worldView.strategyFacts.remove("yitian");else q.worldView.strategyFacts.insert("yitian",y);};publish();
 if(!p||p->getRoom()!=&r||!r.roomRuntime()||r.roomRuntime()->isClosing()||!r.getPlayers().contains(p)
  ||!q.decisionId||q.viewerObjectName!=p->objectName()||q.worldView.self.objectName!=p->objectName()
  ||q.stateRevision!=r.roomRuntime()->stateRevision()||q.kind!=AIRequest::Choice
  ||q.choiceOptions.question!="askForChoice")return;
 if(q.choiceOptions.reason=="weiwudi_guixin"&&q.choiceOptions.choices.join('+')=="wei+shu+wu+qun"){
  // Native yitian.cpp382–397 retains the publicly selected target only during
  // this kingdom question; null QVariant::toPlayer remains source nil.
  const auto *target=r.getTag("Guixin2Modify").value<ServerPlayer*>();
  QJsonValue value(false);
  if(target){if(!r.getAllPlayers(true).contains(const_cast<ServerPlayer*>(target)))return;value=target->objectName();}
  q.choiceOptions.context.insert("guixin_modify_target",value);q.worldView.decisionContext.insert("guixin_modify_target",value);
 }
 if(q.choiceOptions.reason=="shenjun"&&q.choiceOptions.choices.join('+')=="male+female"){
  QJsonObject genders;
  const auto project=[&](const AIPlayerView&v){
   ServerPlayer *target=nullptr;for(auto *a:r.getAllPlayers(true))if(a->objectName()==v.objectName){target=a;break;}
   if(!target||v.generalName.isEmpty()||(target!=p&&!target->hasShownGeneral()))return;
   const auto *g=target->getGeneral();if(!g||g->objectName()!=v.generalName)return;
   genders.insert(v.objectName,g->isMale());
  };
  project(q.worldView.self);for(const auto&v:q.worldView.players)project(v);
  y.insert("general_is_male",genders);publish();
 }
}

#include "ai-strategy-round49-current.h"
#include "ai.h"
#include "card.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonObject>
void projectAIRound49CurrentFields(AIRequest&r,Room&room,ServerPlayer*p){
 auto props=r.worldView.strategyFacts.value("owner_skill_properties").toObject();props.remove("QuxiDeathPlayer");
 auto facts=r.worldView.strategyFacts.value("tenyear_hc").toObject();facts.remove("ShawuTarget");
 const auto publish=[&]{
  if(props.isEmpty())r.worldView.strategyFacts.remove("owner_skill_properties");else r.worldView.strategyFacts.insert("owner_skill_properties",props);
  if(facts.isEmpty())r.worldView.strategyFacts.remove("tenyear_hc");else r.worldView.strategyFacts.insert("tenyear_hc",facts);
 };publish();
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||!r.decisionId||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.stateRevision!=room.roomRuntime()->stateRevision()||!room.getPlayers().contains(p)
  ||r.kind!=AIRequest::UseCard||(r.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE&&r.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE))return;
 if(r.pattern=="@@quxi2"&&r.prompt=="@quxi2"&&r.handlingMethod==Card::MethodNone){
  // ol.cpp11718–20 public property write -> question -> clear. Empty stays empty.
  // The AI's getAliveSiblings excludes the actual dead target; do not repair it.
  props.insert("QuxiDeathPlayer",p->property("QuxiDeathPlayer").toString());
 }else if(r.pattern=="@@shawu"&&r.prompt.startsWith("@shawu:")){
  // tenyear2.cpp28792–94 exact owner tag -> prompted target -> clear.
  const QVariant tag=p->getTag("ShawuTarget");ServerPlayer*target=tag.value<ServerPlayer*>();
  if(!target){if(!tag.isValid()||tag.isNull())facts.insert("ShawuTarget",false);}
  else if(room.getPlayers().contains(target)&&target->getRoom()==&room
   &&r.prompt=="@shawu:"+target->objectName())facts.insert("ShawuTarget",target->objectName());
 }
 publish();
}

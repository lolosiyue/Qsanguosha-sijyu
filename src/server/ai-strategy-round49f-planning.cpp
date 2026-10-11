#include "ai-strategy-round49f-planning.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
void projectAIRound49fPlanning(AIRequest&r,Room&room,const ServerPlayer*p){
 auto facts=r.worldView.strategyFacts.value("hunlie").toObject();
 facts.remove("st_AI");facts.remove("lunce_condition_AI");
 r.worldView.strategyFacts.insert("hunlie",facts);
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.stateRevision!=room.roomRuntime()->stateRevision()
  ||r.kind!=AIRequest::Choice||r.choiceOptions.question!="askForChoice")return;
 // Fixed sgkgodlunce writes each own selected string immediately before this
 // respective question. Read exactly that current stored string, not a cached
 // previous AI choice, inferred prompt or a foreign recipient's strategy tag.
 const QString key=r.choiceOptions.reason=="lunce_condition"?QStringLiteral("st_AI")
  :r.choiceOptions.reason=="lunce_effect"?QStringLiteral("lunce_condition_AI"):QString();
 if(key.isEmpty())return;
 facts.insert(key,p->getTag(key).toString());
 r.worldView.strategyFacts.insert("hunlie",facts);
}

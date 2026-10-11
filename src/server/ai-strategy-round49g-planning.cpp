#include "ai-strategy-round49g-planning.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
void projectAIRound49gPlanning(AIRequest&r,Room&room,const ServerPlayer*p){
 auto facts=r.worldView.strategyFacts.value("sgs10th").toObject();facts.remove("ny_10th_yijia");
 r.worldView.strategyFacts.insert("sgs10th",facts);
 if(!p||p->getRoom()!=&room||!room.roomRuntime()||room.roomRuntime()->isClosing()
  ||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
  ||r.stateRevision!=room.roomRuntime()->stateRevision()||r.choiceOptions.reason!="ny_10th_yijia")return;
 if(!((r.kind==AIRequest::PlayerChosen&&r.choiceOptions.question=="askForPlayerChosen")
   ||(r.kind==AIRequest::CardChosen&&r.choiceOptions.question=="askForCardChosen"
      &&r.choiceOptions.choices==QStringList{QStringLiteral("e")})))return;
 // Native owner writes the damaged recipient's objectName before both questions.
 // Preserve the source string (including known empty / no matching roster name);
 // only the existing lawful roster resolver interprets it as a Player.
 facts.insert("ny_10th_yijia",p->getTag("ny_10th_yijia").toString());
 r.worldView.strategyFacts.insert("sgs10th",facts);
}

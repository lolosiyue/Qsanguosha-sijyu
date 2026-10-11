#include "ai-strategy-round41-context.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QHash>
#include <QJsonObject>
#include <QObject>

namespace {
const QStringList names={"FiveFanjianPindianTarget","FourTianxiang",
    "feijiangtsTarget","luajueshengTarget"};
class Observations final:public QObject {
public:
 explicit Observations(Room &room):QObject(&room){}
 // Pointer identity, not player name: deletion never transfers observations to a
 // replacement player. Room owns this across isolated VM recreation.
 QHash<ServerPlayer *,QJsonObject> flags;
};
Observations *observations(Room &room,bool create=false){
 for(QObject *c:room.children())if(auto *o=dynamic_cast<Observations *>(c))return o;
 return create?new Observations(room):nullptr;
}
void watch(Observations *o,ServerPlayer *p){
 if(o->flags.contains(p))return;
 o->flags.insert(p,{});
 QObject::connect(p,&QObject::destroyed,o,[o,p]{o->flags.remove(p);});
}
QJsonObject values(Room &room,ServerPlayer *viewer,const QString &key){
 QJsonObject rows;const auto *o=observations(room);
 for(ServerPlayer *p:room.getPlayers()){
  if(p==viewer){rows.insert(p->objectName(),QJsonObject{{"status","exact"},
   {"value",p->hasFlag(key)},{"basis","current_owner_flag"}});continue;}
  if(!o)continue;
  const auto observation=o->flags.value(p).value(key);
  if(!observation.isBool())continue;
  // Deliberately no current native hasFlag test, revision stamp, private
  // mutation listener, or conditional invalidation here. Those would reveal
  // unseen writes through a changed value OR a changed known/unknown status.
  rows.insert(p->objectName(),QJsonObject{{"status","estimate"},
   {"value",observation},{"basis","skill_flag_last_public_observation"}});
 }
 return {{"viewer",viewer->objectName()},{"flags",QJsonObject{{key,rows}}}};
}
void publish(AIWorldView &w,Room &room,ServerPlayer *viewer,const QString &key){
 w.strategyFacts.remove("round41_skill_flags");
 if(key.isEmpty()||!viewer||viewer->getRoom()!=&room
  ||w.self.objectName!=viewer->objectName()||room.roomRuntime()->isClosing())return;
 w.strategyFacts.insert("round41_skill_flags",values(room,viewer,key));
}
}
void initializeAIRound41SkillFlags(Room &room,ServerPlayer *p){
 if(!p||p->getRoom()!=&room)return;
 auto *o=observations(room,true);if(o->flags.contains(p))return;watch(o,p);
 // The call site is ServerPlayer's constructor after the Player base has
 // initialized its empty flag set. A late/nonempty call grants no baseline.
 if(!p->getFlagList().isEmpty())return;
 QJsonObject initial;for(const auto &name:names)initial.insert(name,false);
 o->flags.insert(p,initial);
}
void observeAIRound41PublicSkillFlag(Room &room,ServerPlayer *p,const QString &operation){
 if(!p||p->getRoom()!=&room||!room.getPlayers().contains(p))return;
 const QString key=operation.startsWith('-')?operation.mid(1):operation;
 if(operation!="."&&!names.contains(key))return;
 auto *o=observations(room,true);watch(o,p);auto row=o->flags.value(p);
 // The notification itself is the observation. Never sample a foreign native
 // flag when reading the ledger. An unchanged repeated broadcast is still valid.
 if(operation==".")for(const auto &name:names)row.insert(name,false);
 else row.insert(key,!operation.startsWith('-'));
 o->flags.insert(p,row);
}
void projectAIRound41SkillFlagQuestion(AIRequest &r,Room &room,ServerPlayer *viewer){
 r.worldView.strategyFacts.remove("round41_skill_flags");
 if(!viewer||r.viewerObjectName!=viewer->objectName()
  ||r.stateRevision!=room.roomRuntime()->stateRevision())return;
 if(r.kind==AIRequest::RespondCard&&r.choiceOptions.question=="askForPindian"
  &&r.choiceOptions.reason=="FiveFanjian")
  publish(r.worldView,room,viewer,"FiveFanjianPindianTarget");
}
void projectAIRound41SkillFlagEvent(AIWorldView &w,Room &room,ServerPlayer *viewer,
 const AIEventView &event){
 w.strategyFacts.remove("round41_skill_flags");
 // Keep existing actor-private ChoiceMade admission. Do not turn internal
 // skill invoke/choice notifications into a broadcast to additional viewers.
 if(!viewer||event.triggerEvent!=ChoiceMade||event.kind!="choice"
  ||event.from!=viewer->objectName()||!event.privateEvent
  ||event.privateViewer!=viewer->objectName())return;
 const QString kind=event.details.value("choice_kind").toString();QString key;
 if(kind=="skillChoice"&&event.reason=="FourTianxiang")key="FourTianxiang";
 if(kind=="skillInvoke"&&event.reason=="feijiangts")key="feijiangtsTarget";
 if(kind=="skillInvoke"&&event.reason=="luajuesheng")key="luajueshengTarget";
 publish(w,room,viewer,key);
}

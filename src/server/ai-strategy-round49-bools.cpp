#include "ai-strategy-round49-bools.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonObject>
void projectAIRound49OwnerBools(AIRequest&q,Room&r,ServerPlayer*p){
 auto row=q.worldView.strategyFacts.value("owner_skill_properties").toObject();
 const QStringList keys={"zunwei_draw","zunwei_recover","zunwei_equip"};for(const auto&k:keys)row.remove(k);
 const auto publish=[&]{if(row.isEmpty())q.worldView.strategyFacts.remove("owner_skill_properties");else q.worldView.strategyFacts.insert("owner_skill_properties",row);};publish();
 if(!p||!r.getPlayers().contains(p)||p->getRoom()!=&r||!r.roomRuntime()||r.roomRuntime()->isClosing()||!q.decisionId||q.viewerObjectName!=p->objectName()||q.worldView.self.objectName!=p->objectName()||q.stateRevision!=r.roomRuntime()->stateRevision()||q.kind!=AIRequest::Activate||q.reason!=CardUseStruct::CARD_USE_REASON_PLAY)return;
 // Native ZunweiCard effect27090–27111 is the current public owner writer.
 // Absent QVariant converts false exactly; no history or global choice grant.
 for(const auto&k:keys)row.insert(k,p->property(k.toUtf8().constData()).toBool());
 if(q.stateRevision!=r.roomRuntime()->stateRevision())return;publish();
}

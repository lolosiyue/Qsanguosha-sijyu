#include "ai-strategy-round34-choice.h"
#include "ai.h"
#include "room.h"
#include "serverplayer.h"
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
namespace {
class FixedDistanceLedger final : public QObject {
public:
 explicit FixedDistanceLedger(Room &room) : QObject(&room) { setObjectName("isolated-round34-fixed-distance"); }
 // Viewer -> source -> targets. These are notification witnesses, not native maps.
 QHash<const ServerPlayer *, QHash<const Player *, QSet<const Player *>>> rows;
 QSet<const Player *> connected;
 void invalidate(const Player *p) {
  for (auto &sources : rows) {
   sources.remove(p);
   for (auto &targets : sources) targets.remove(p);
  }
 }
};
FixedDistanceLedger *ledger(Room &room, bool create) {
 auto *p=room.findChild<QObject *>("isolated-round34-fixed-distance",Qt::FindDirectChildrenOnly);
 if (!p && create) return new FixedDistanceLedger(room);
 return static_cast<FixedDistanceLedger *>(p);
}
bool roster(Room &room,const Player *p) {
 return p && room.findPlayerByObjectName(p->objectName(),true)==p;
}
void connectPlayer(FixedDistanceLedger *state, const Player *p) {
 if (state->connected.contains(p)) return;
 state->connected.insert(p);
 QObject::connect(p,&Player::gameplay_property_changed,state,[state,p]{state->invalidate(p);});
 QObject::connect(p,&QObject::destroyed,state,[state,p]{
  state->invalidate(p);state->connected.remove(p);
  // ServerPlayer is the only admitted viewer. Avoid casts during destruction.
  for(auto it=state->rows.begin();it!=state->rows.end();) {
   if(static_cast<const Player *>(it.key())==p) it=state->rows.erase(it);else ++it;
  }
 });
}
}
void observeAIRound34FixedDistance(Room &room,Player *from,const Player *to,int distance,
                                  bool set,const QList<ServerPlayer *> &notifiedPlayers) {
 if(!roster(room,from)||!roster(room,to))return;
 auto *state=ledger(room,true);
 connectPlayer(state,from);connectPlayer(state,to);
 // A removal gives no proof of a remaining constant. Never inspect the whole map.
 for(auto &sources:state->rows) sources[from].remove(to);
 if(!set || from->fixedDistanceCount(to,distance)<1)return;
 for(auto *viewer:notifiedPlayers) {
  if(!roster(room,viewer))continue;
  connectPlayer(state,viewer);
  state->rows[viewer][from].insert(to);
 }
}
void projectAIRound34Choice(AIWorldView &world,Room &room,const ServerPlayer *viewer) {
 QJsonObject result;
 const auto *state=ledger(room,false);
 if(state&&roster(room,viewer)&&world.self.objectName==viewer->objectName()) {
  const auto sources=state->rows.value(viewer);
  for(auto it=sources.cbegin();it!=sources.cend();++it) {
   if(!roster(room,it.key()))continue;
   QJsonObject row;
   for(const auto *target:it.value())if(roster(room,target))row.insert(target->objectName(),true);
   if(!row.isEmpty())result.insert(it.key()->objectName(),row);
  }
 }
 world.strategyFacts.insert("distance_fixed_witness",result);
}

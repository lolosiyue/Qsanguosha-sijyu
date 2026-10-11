#ifndef AI_STRATEGY_ROUND30_PLANNING_H
#define AI_STRATEGY_ROUND30_PLANNING_H
#include <QJsonObject>
class Room;
class ServerPlayer;
struct AIRequest;
// Capture exactly once before any policy entry. AI VM reset must not recapture.
void initializeAIRound30TargetRevises(Room &);
// Mirror every instrumented policy entry. A different viewer invalidates shared
// private strategy continuity; legacy/fallback/external execution invalidates it.
void beginAIRound30TargetRevises(Room &, const QString &viewer);
void invalidateAIRound30TargetRevises(Room &);
// Fixed source Player property aiNoTo only. Host supplies active native request.
// spec = {kind:"target_revise_property",target:string,op:"read"|"write",value?:bool}
QJsonObject queryAIRound30TargetRevises(Room &, const AIRequest &,
    const ServerPlayer *viewer, const QJsonObject &spec);
#endif

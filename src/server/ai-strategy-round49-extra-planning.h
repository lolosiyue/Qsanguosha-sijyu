#ifndef AI_STRATEGY_ROUND49_EXTRA_PLANNING_H
#define AI_STRATEGY_ROUND49_EXTRA_PLANNING_H
#include <QJsonObject>
#include <QVariant>
struct AIRequest; class Room; class ServerPlayer;
// Fixed tenyear_hc source hypotheses, including the reached mutation prefixes.
// No native provider selection, payment receipt, hidden card or new rule grant.
bool aiRound49ExtraPlanningConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
void projectAIRound49ExtraPlanningContext(AIRequest&,const QVariant&);
#endif

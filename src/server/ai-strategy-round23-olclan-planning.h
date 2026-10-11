#ifndef AI_STRATEGY_ROUND23_OLCLAN_PLANNING_H
#define AI_STRATEGY_ROUND23_OLCLAN_PLANNING_H
#include <QJsonObject>
struct AIRequest;
class Room;
class ServerPlayer;
// Audited source: olClan.lua3260–3327, AI olClan-ai.lua1882–1888.
// This predicate grants only hypothetical Play planning, never a card action.
bool aiRound23OlClanPlanningQuestion(const AIRequest &, Room &, const ServerPlayer *);
QJsonObject aiRound23OlClanPlanningContext(const AIRequest &, Room &, const ServerPlayer *);
#endif

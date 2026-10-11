#ifndef AI_STRATEGY_ROUND41_PLANNING_H
#define AI_STRATEGY_ROUND41_PLANNING_H
#include <QJsonObject>
class AIRequest; class Room; class ServerPlayer;
// Fixed FC DIY constructor hypotheses. Never called by payment admission.
bool aiRound41PlanningConstruction(const AIRequest &, Room &, const ServerPlayer *, const QJsonObject &);
// Additional lawful own ShenJian materials, after full constructor validation.
bool aiRound41PlanningPileMaterial(const AIRequest &, Room &, const ServerPlayer *, const QJsonObject &, int);
#endif

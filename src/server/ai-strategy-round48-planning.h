#ifndef AI_STRATEGY_ROUND48_PLANNING_H
#define AI_STRATEGY_ROUND48_PLANNING_H
#include <QJsonObject>
class Room; class ServerPlayer; class Card; struct AIRequest;
bool aiRound48PlanningConstruction(const AIRequest &, Room &, const ServerPlayer *, const QJsonObject &);
QJsonObject aiRound48YantaoluanSlash(const AIRequest &, Room &, const ServerPlayer *, const QJsonObject &, const Card *);
#endif

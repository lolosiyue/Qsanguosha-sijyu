#ifndef AI_STRATEGY_ROUND43_PLANNING_H
#define AI_STRATEGY_ROUND43_PLANNING_H
#include <QJsonObject>
struct AIRequest;class Room;class ServerPlayer;
bool aiRound43PlanningConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
#endif

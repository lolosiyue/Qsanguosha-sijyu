#ifndef AI_STRATEGY_ROUND49_PLANNING_H
#define AI_STRATEGY_ROUND49_PLANNING_H
#include <QJsonObject>
class Room;class ServerPlayer;struct AIRequest;
bool aiRound49PlanningConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
#endif

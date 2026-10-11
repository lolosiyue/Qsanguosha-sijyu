#ifndef AI_STRATEGY_ROUND42_PLANNING_H
#define AI_STRATEGY_ROUND42_PLANNING_H
#include <QJsonObject>
#include <QString>
struct AIRequest;
class Room;class ServerPlayer;class Card;
bool aiRound42PlanningConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
int aiRound42PlanningExtraTarget(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
bool aiRound42PlanningSource(const AIRequest&,Room&,const ServerPlayer*,const Card*,const QString&);
QJsonObject aiRound42PlanningTargetFilter(Room&,ServerPlayer*,const AIRequest&,const Card*,const QString&,const QJsonObject&);
int aiRound42PlanningPrototype(const AIRequest&,const ServerPlayer*,const QString&,const QString&);
#endif

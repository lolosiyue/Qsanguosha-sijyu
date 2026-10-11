#ifndef AI_STRATEGY_ROUND25_PLANNING_H
#define AI_STRATEGY_ROUND25_PLANNING_H
#include <QJsonObject>
#include <functional>
struct AIRequest;struct AIWorldView;class Room;class ServerPlayer;class Card;class QVariant;
bool aiRound25PlanningConstruction(const AIRequest&,const ServerPlayer*,const QJsonObject&,QString*error=nullptr);
void projectAIRound25PlanningContext(AIRequest&,Room&,const ServerPlayer*,const QVariant&,const std::function<QJsonObject(const Card*)>&);
void captureAIRound25PlanningMetadata(Room&);
void projectAIRound25PlanningMetadata(AIWorldView&,Room&);
#endif

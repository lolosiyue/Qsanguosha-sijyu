#ifndef AI_STRATEGY_ROUND45_PLANNING_H
#define AI_STRATEGY_ROUND45_PLANNING_H
#include <QJsonObject>
struct AIRequest; struct AIWorldView; class Room; class ServerPlayer; class Card;
// Readonly source recipes only. Neither function authorizes selection/payment.
bool aiRound45PlanningConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
bool aiRound45PlanningProduct(const QJsonObject&,const Card&);
QJsonValue aiRound45PlanningHandPile(const AIWorldView&,Room&,const ServerPlayer*);
bool aiRound45PlanningRepeatedMaterials(const QJsonObject&);
void projectAIRound45PlanningFacts(AIWorldView&,Room&,const ServerPlayer*);
#endif

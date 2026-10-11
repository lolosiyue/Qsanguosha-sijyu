#ifndef AI_STRATEGY_ROUND28_PLANNING_H
#define AI_STRATEGY_ROUND28_PLANNING_H
#include <QJsonObject>
#include <QVariant>
#include <functional>
struct AIRequest; class Room; class ServerPlayer; class Card;
// All profiles borrow the actual current-question card. No cloned identity or payment.
using AIRound28PlanningProfile = std::function<QJsonObject(const Card *, int, const QString &)>;
void projectAIRound28PlanningContext(AIRequest &, Room &, const ServerPlayer *, const QVariant &,
    QJsonObject &, const std::function<QJsonObject(const Card *)> &, const AIRound28PlanningProfile & = {});
bool aiRound28PlanningConstruction(const AIRequest &, Room &, const ServerPlayer *, const QJsonObject &);
const Card *aiRound28PlanningEvaluation(const AIRequest &, Room &, const ServerPlayer *, const QString &);
int aiRound28PlanningExtraTarget(const AIRequest &, Room &, const ServerPlayer *, const QString &);
QJsonObject aiRound28PlanningTargetFilter(Room &, ServerPlayer *, const AIRequest &, const Card *,
    const QString &, const QJsonObject &);
#endif

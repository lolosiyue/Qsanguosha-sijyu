#ifndef AI_STRATEGY_ROUND21_PLANNING_H
#define AI_STRATEGY_ROUND21_PLANNING_H
#include <QJsonObject>
#include <QString>
#include <functional>
struct AIRequest;
class ServerPlayer;
class Room;
class Card;
// Readonly fixed source constructor admission, never payment authorization.
bool aiRound21PlanningConstruction(const AIRequest &, const ServerPlayer *,
    const QJsonObject &spec, QString *error = nullptr);
// -1 absent; bit0 paid materials, bit1 declared userString. Native live source
// selection/factory validation is still mandatory at receipt/final rebuild.
int aiRound21PlanningPrototype(const AIRequest &, const ServerPlayer *,
    const QString &skill, const QString &prototype);
void projectAIRound21PlanningContext(AIRequest &, Room &, const ServerPlayer *,
    const std::function<QJsonObject(const Card *)> &publicCard);
#endif

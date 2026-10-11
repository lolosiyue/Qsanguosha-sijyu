#ifndef AI_STRATEGY_ROUND27_PLANNING_H
#define AI_STRATEGY_ROUND27_PLANNING_H
#include <QJsonObject>
#include <QVariant>
#include <functional>
struct AIRequest;
class Room;
class ServerPlayer;
class Card;
// Fixed fcDIY15975-16010: only this owner's invocation or this recipient's
// currently offered Huanshi cards. Results classify; they never authorize cost.
void projectAIRound27PlanningContext(AIRequest &, Room &, const ServerPlayer *,
    const QVariant &, QJsonObject &, const std::function<QJsonObject(const Card *)> &,
    const std::function<QJsonObject(const Card *)> &judgeProfile = {});
#endif

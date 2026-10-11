#ifndef AI_STRATEGY_YCZH2017_H
#define AI_STRATEGY_YCZH2017_H
#include <QJsonObject>
#include <functional>
struct AIRequest;
class Room;
class ServerPlayer;
class Card;
void projectAIStrategyYCZH2017(AIRequest &, Room &, ServerPlayer *,
    const std::function<QJsonObject(const Card *)> &publicDecisionCard);
#endif

#ifndef AI_STRATEGY_CONSTRUCTION_CONTEXT_H
#define AI_STRATEGY_CONSTRUCTION_CONTEXT_H
#include <QJsonObject>
#include <functional>
struct AIRequest;
class Card;
class Room;
class ServerPlayer;
// Only the current named inquiry receives its source-specific observed data.
void projectAIConstructionContext(AIRequest &, Room &, const ServerPlayer *,
    const std::function<QJsonObject(const Card *)> &describeCard);
#endif

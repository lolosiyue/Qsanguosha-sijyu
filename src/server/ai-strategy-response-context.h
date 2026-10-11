#ifndef AI_STRATEGY_RESPONSE_CONTEXT_H
#define AI_STRATEGY_RESPONSE_CONTEXT_H
#include <QJsonObject>
#include <QVariant>
#include <functional>
struct AIRequest;
struct AIWorldView;
struct AIEventView;
class Room;
class ServerPlayer;
class Card;
// Fixed audited questions only. publicCard must redact undisclosed backing IDs.
void projectAIStrategyResponseContext(AIRequest &, Room &, ServerPlayer *,
    const QVariant &, QJsonObject &context,
    const std::function<QJsonObject(const Card *)> &publicCard);
void projectAIStrategyResponseEventContext(AIWorldView &, Room &, ServerPlayer *,
    const AIEventView &, const std::function<QJsonObject(const Card *)> &publicCard);
#endif

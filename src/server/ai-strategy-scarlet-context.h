#ifndef AI_STRATEGY_SCARLET_CONTEXT_H
#define AI_STRATEGY_SCARLET_CONTEXT_H
#include <QJsonObject>
#include <QVariant>
#include <functional>
struct AIRequest;
struct AIWorldView;
struct AIEventView;
class Room;
class ServerPlayer;
class Card;
void projectAIStrategyScarletContext(AIRequest &, Room &, ServerPlayer *, const QVariant &,
    QJsonObject &, const std::function<QJsonObject(const Card *)> &);
void projectAIStrategyScarletEventContext(AIWorldView &, Room &, ServerPlayer *, const AIEventView &,
    const std::function<QJsonObject(const Card *)> &);
#endif

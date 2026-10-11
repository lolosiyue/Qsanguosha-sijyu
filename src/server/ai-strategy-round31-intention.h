#ifndef AI_STRATEGY_ROUND31_INTENTION_H
#define AI_STRATEGY_ROUND31_INTENTION_H
#include <QJsonObject>
#include <functional>
struct AIWorldView;
struct AIEventView;
class Room;
class ServerPlayer;
class QVariant;
class Card;
void projectAIRound31IntentionWorld(AIWorldView &, Room &, ServerPlayer *);
void projectAIRound31IntentionEvent(AIEventView &, const AIWorldView &, Room &,
    ServerPlayer *observer, ServerPlayer *eventPlayer, const QVariant &,
    const std::function<QJsonObject(const Card *)> &publicCard);
#endif

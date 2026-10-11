#ifndef AI_STRATEGY_ROUND21_ORIGINAL_H
#define AI_STRATEGY_ROUND21_ORIGINAL_H
#include <QJsonObject>
#include <QVariant>
struct AIWorldView;
struct AIRequest;
struct AIEventView;
class Room;
class ServerPlayer;
void projectAIStrategyRound21OriginalWorld(AIWorldView &, Room &, ServerPlayer *);
void projectAIStrategyRound21OriginalRequest(AIRequest &, Room &, ServerPlayer *, const QVariant &, QJsonObject &);
void projectAIStrategyRound21OriginalEvent(AIWorldView &, Room &, ServerPlayer *, const AIEventView &);
#endif

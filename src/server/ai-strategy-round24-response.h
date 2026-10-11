#ifndef AI_STRATEGY_ROUND24_RESPONSE_H
#define AI_STRATEGY_ROUND24_RESPONSE_H
#include <QJsonObject>
#include <QVariant>
struct AIRequest;
class Room;
class ServerPlayer;
// Only nyarz_huanshi Discard/AG and kanyu Invoke/AG. No receipt/card custody.
void projectAIStrategyRound24Response(AIRequest &, Room &, ServerPlayer *,
                                     const QVariant &, QJsonObject &context);
void clearAIStrategyRound24Response(Room &);
#endif

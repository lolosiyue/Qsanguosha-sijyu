#ifndef AI_STRATEGY_ROUND21_RESPONSE_H
#define AI_STRATEGY_ROUND21_RESPONSE_H
#include <QJsonObject>
struct AIRequest;
class Room;
class ServerPlayer;
// Only the exact sr_yinmeng CardShow question. Requires a face already lawfully
// present in the viewer's snapshot; the source tag alone grants no visibility.
void projectAIStrategyRound21Response(AIRequest &, Room &, ServerPlayer *, QJsonObject &context);
#endif

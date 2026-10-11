#ifndef AI_STRATEGY_ROUND26_RESPONSE_H
#define AI_STRATEGY_ROUND26_RESPONSE_H
#include <QJsonObject>
#include <QString>
struct AIRequest;
struct AIWorldView;
struct AIEventView;
class Room;
class ServerPlayer;
// True isolated-runtime bootstrap only: immutable absence metadata, never a
// mutable gameplay choice or another viewer's strategy memory.
void captureAIStrategyRound26Response(Room &);
void clearAIStrategyRound26Response(Room &);
void projectAIStrategyRound26Response(AIRequest &, Room &, ServerPlayer *);
void projectAIStrategyRound26ResponseEvent(AIWorldView &, Room &, ServerPlayer *, const AIEventView &);

// Call only after an actual all-player flags broadcast. Finite audited literal.
void recordAIStrategyRound26PublicFlag(Room &, ServerPlayer *, const QString &operation);
QJsonObject readAIStrategyRound26PublicFlag(Room &, ServerPlayer *viewer, ServerPlayer *target, const QString &flag);
#endif

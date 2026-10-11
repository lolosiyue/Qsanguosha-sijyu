#ifndef AI_STRATEGY_HISTORY_CONTEXT_H
#define AI_STRATEGY_HISTORY_CONTEXT_H
#include "ai-strategy-history.h"
struct AIWorldView;
struct AIEventView;
class Room;
class ServerPlayer;
// Native configuration determines identity; Lua cannot choose a profile or path.
void observeAIStrategyHistory(AIWorldView &, Room &, const AIEventView &);
void projectAIStrategyHistory(AIWorldView &, Room &, ServerPlayer *);
std::unique_ptr<AIStrategyHistoryWrite> prepareAIStrategyHistory(
    const AIWorldView &, Room &, const QString &revision, const QJsonObject &data, QString *error);
#endif

#ifndef AI_STRATEGY_EVENTS_H
#define AI_STRATEGY_EVENTS_H
#include "ai.h"
class Room;
class ServerPlayer;
void projectAIStrategyEvent(AIEventView &, const AIWorldView &, Room &, ServerPlayer *observer,
                           ServerPlayer *target, const QVariant &data);
// Adds only observations previously authorized for this exact viewer. The
// native ledger belongs to the Room lifetime and never crosses into Lua.
void projectAIStrategyKnownCards(AIWorldView &, Room &, ServerPlayer *observer);
#endif

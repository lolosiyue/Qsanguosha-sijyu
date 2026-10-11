#ifndef AI_STRATEGY_DRAGON_EVENTS_H
#define AI_STRATEGY_DRAGON_EVENTS_H
#include "ai.h"
// Only immutable public event/world facts. The existing Room::broadcastProperty
// role -> revealRole route records actual public disclosure; no private role tag
// or additional observer ledger is needed here.
void projectAIStrategyDragonEvent(AIWorldView &world, const AIEventView &event);
#endif

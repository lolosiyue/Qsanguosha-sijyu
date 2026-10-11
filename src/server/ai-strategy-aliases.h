#ifndef AI_STRATEGY_ALIASES_H
#define AI_STRATEGY_ALIASES_H

class Room;
struct AIWorldView;

// Call once after the original AI has loaded, before any isolated decision.
// Captures only audited registry function identity; no callback is executed.
void captureAIStrategyAliases(Room &room);

// Copies the immutable, Room-owned capture; never reads the gameplay VM here.
void projectAIStrategyAliases(AIWorldView &world, Room &room);

#endif

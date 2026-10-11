#ifndef AI_STRATEGY_ROUND31_CHOICE_H
#define AI_STRATEGY_ROUND31_CHOICE_H
struct AIWorldView;
class Room;
class ServerPlayer;
// Source smart-ai.lua:7212 inspects only these two visibility flags. A lawful
// remembered/canSeeHandcard face alone does not satisfy that policy predicate.
void projectAIRound31Choice(AIWorldView &, Room &, const ServerPlayer *);
#endif

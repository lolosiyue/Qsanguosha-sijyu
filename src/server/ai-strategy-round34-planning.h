#ifndef AI_STRATEGY_ROUND34_PLANNING_H
#define AI_STRATEGY_ROUND34_PLANNING_H
struct AIWorldView;
class Room;
class ServerPlayer;
// Public engine definitions and viewer-visible hand/Ox counts only. No action
// receipt, draw-pile ordering, foreign hidden face, or foreign observer rights.
void projectAIRound34PlanningCardCounts(AIWorldView &, Room &, const ServerPlayer *);
#endif

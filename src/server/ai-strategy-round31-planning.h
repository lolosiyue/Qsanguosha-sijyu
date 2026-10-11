#ifndef AI_STRATEGY_ROUND31_PLANNING_H
#define AI_STRATEGY_ROUND31_PLANNING_H
class Room;
struct AIWorldView;
// Invoke once after normal legacy AI autoload, before any strategy executes.
void captureAIRound31PlanningMetadata(Room &room);
void projectAIRound31PlanningMetadata(AIWorldView &world, Room &room);
#endif

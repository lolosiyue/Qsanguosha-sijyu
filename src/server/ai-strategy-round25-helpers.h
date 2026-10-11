#ifndef AI_STRATEGY_ROUND25_HELPERS_H
#define AI_STRATEGY_ROUND25_HELPERS_H
class Room;
class ServerPlayer;
struct AIWorldView;
struct AIEventView;
// Finite public policy counters; no generic mark/tag or legacy-AI lookup.
void projectAIRound25ImagineWorld(AIWorldView &, Room &, ServerPlayer *viewer);
// Invoke only from the real native event dispatch, after building that viewer's
// world. The validator matches this witness before accepting a counter delta.
void projectAIRound25ImagineEvent(AIWorldView &, Room &, ServerPlayer *viewer,
                                 const AIEventView &);
#endif

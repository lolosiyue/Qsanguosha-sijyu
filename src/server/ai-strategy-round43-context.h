#ifndef AI_STRATEGY_ROUND43_CONTEXT_H
#define AI_STRATEGY_ROUND43_CONTEXT_H
struct AIRequest;
class Room;
class ServerPlayer;
// Exact current-owner toInt at the named PlayerChosen inquiry. No history cache.
void projectAIRound43Qingsi(AIRequest &, Room &, ServerPlayer *);
#endif

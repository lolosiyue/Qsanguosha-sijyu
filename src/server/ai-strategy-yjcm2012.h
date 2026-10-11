#ifndef AI_STRATEGY_YJCM2012_H
#define AI_STRATEGY_YJCM2012_H
struct AIWorldView;
class Room;
class ServerPlayer;
// Owner tags and narrow public legality facts. No legacy strategy execution.
void projectAIStrategyYjcm2012(AIWorldView &, Room &, ServerPlayer *);
#endif

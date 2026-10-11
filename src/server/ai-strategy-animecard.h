#ifndef AI_STRATEGY_ANIMECARD_H
#define AI_STRATEGY_ANIMECARD_H
class Room;
class ServerPlayer;
struct AIRequest;
struct AIWorldView;
// Record only the target accepted by the actual reijyuu PlayerChosen inquiry.
void witnessAIStrategyReijyuuChoice(Room &room, const ServerPlayer *viewer,
                                  const ServerPlayer *target);
// Project only that viewer's witnessed target for the following Choice prompt.
// Missing/stale witness stays absent (unknown), never known-empty.
void projectAIStrategyReijyuuChoice(Room &room, const ServerPlayer *viewer,
                                  AIRequest &request);
void projectAIStrategyAnimecardCatalog(AIWorldView &world, Room &room,
                                      const ServerPlayer *viewer);
#endif

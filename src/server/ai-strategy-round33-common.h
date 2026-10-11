#ifndef AI_STRATEGY_ROUND33_COMMON_H
#define AI_STRATEGY_ROUND33_COMMON_H
class Room;
class ServerPlayer;
class Card;
struct AIRequest;
// Native call site only: invoke inside decideNullification after the actual
// trick/from/to context is assembled. No Lua card/tag query is exposed.
void projectAIRound33AmazingGrace(AIRequest &request, Room &room,
                                 ServerPlayer *viewer, const Card *trick,
                                 ServerPlayer *from, ServerPlayer *to);
#endif

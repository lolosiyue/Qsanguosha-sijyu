#ifndef AI_STRATEGY_ROUND42_CONTEXT_H
#define AI_STRATEGY_ROUND42_CONTEXT_H
struct AIRequest;
class Room;
class ServerPlayer;
// Finite current-owner tag read only. No persistent tag/recipient/card authority.
void projectAIRound42Liehou(AIRequest &, Room &, ServerPlayer *);
#endif

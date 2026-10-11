#ifndef AI_STRATEGY_ROUND47D_DAPO_CONTEXT_H
#define AI_STRATEGY_ROUND47D_DAPO_CONTEXT_H
class Room;
class ServerPlayer;
class QVariant;
struct AIWorldView;
struct AIEventView;
// Current sanitized Dying observer snapshot only. No persistent observation.
void projectAIRound47DapoEvent(AIWorldView &, Room &, ServerPlayer *viewer,
    ServerPlayer *eventPlayer, const AIEventView &, const QVariant &);
#endif

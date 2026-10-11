#ifndef AI_STRATEGY_ROUND40_CONTEXT_H
#define AI_STRATEGY_ROUND40_CONTEXT_H
#include <QVariant>
struct AIRequest;
struct AIWorldView;
struct AIEventView;
class Room;
class ServerPlayer;
// Call only inside actual LuaAI::filterEvent after its callback==0 guard,
// before invoking the fixed CloneAI thunk. TrustAI/direct recordEvent is not a writer.
void observeAIRound40LegacyFilterEvent(Room &, int event, ServerPlayer *, const QVariant &);
// Full game-Lua initialization/reload, not isolated AI VM recreation.
void clearAIRound40TargetHistory(Room &);
void projectAIRound40TargetHistory(AIRequest &, Room &, ServerPlayer *);
void projectAIRound40AzirEvent(AIWorldView &, Room &, ServerPlayer *viewer,
    ServerPlayer *eventPlayer, const AIEventView &, const QVariant &);
#endif

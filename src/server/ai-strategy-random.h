#ifndef AI_STRATEGY_RANDOM_H
#define AI_STRATEGY_RANDOM_H

#include "ai.h"

class Room;
class ServerPlayer;

// Request-private candidate identities stay native. Lua receives only a token
// and count, and consumes decision RNG when it actually reaches this branch.
void projectAIStrategyRandomRequest(AIRequest &, Room &, ServerPlayer *);
// Tuixinzhifu third-pass source ordering, only when every card is already
// disclosed to this viewer. No filtering, sampling, new faces or authority.
void projectAIStrategyTuixinzhifuThirdPassRequest(AIRequest &, Room &, ServerPlayer *);
bool resolveAIStrategyRandomChoice(Room &, ServerPlayer *, const AIRequest &, AIResult &);

// Dismantlement planning samples the full hand lazily using gameplay RNG. The
// sample is a strategy fact, never a card submission or a revealed hidden ID.
void projectAIStrategyRandomPlanningRequest(AIRequest &, Room &, ServerPlayer *);
QJsonObject resolveAIStrategyRandomPlanningQuery(Room &, ServerPlayer *,
                                               const AIRequest &, const QJsonObject &);
// Only the current viewer's native tag may refresh the donor's remembered set.
// The dispatcher validates active request/revision and charges its query budget.
QJsonObject refreshAIStrategyDiscardPreferences(Room &, ServerPlayer *);

#endif

#ifndef AI_STRATEGY_ROUND41_CONTEXT_H
#define AI_STRATEGY_ROUND41_CONTEXT_H
#include <QString>
struct AIWorldView; struct AIRequest; struct AIEventView;
class Room; class ServerPlayer;
// Actual ServerPlayer construction only, before any flag setter. Not a reset API.
void initializeAIRound41SkillFlags(Room &, ServerPlayer *);
// Call after the real all-client flags-property broadcast, never direct setFlags.
void observeAIRound41PublicSkillFlag(Room &, ServerPlayer *, const QString &);
void projectAIRound41SkillFlagQuestion(AIRequest &, Room &, ServerPlayer *);
void projectAIRound41SkillFlagEvent(AIWorldView &, Room &, ServerPlayer *,
    const AIEventView &);
#endif

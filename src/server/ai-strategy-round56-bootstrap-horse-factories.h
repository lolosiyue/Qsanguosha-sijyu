#ifndef AI_STRATEGY_ROUND56_BOOTSTRAP_HORSE_FACTORIES_H
#define AI_STRATEGY_ROUND56_BOOTSTRAP_HORSE_FACTORIES_H
#include "engine.h"
// Public native catalogue metadata only. Call for BOTH literal horse families
// before configured Lua or private account selection; this is not a source
// token. Room-created Lua horse products still need their full producer proof.
inline bool matchAIRound56BootstrapHorseFactories(const Engine &engine) {
 return engine.aiRound56StandardCloneFactory(QStringLiteral("OffensiveHorse"))
     && engine.aiRound56StandardCloneFactory(QStringLiteral("DefensiveHorse"));
}
#endif

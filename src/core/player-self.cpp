#include "player.h"
#include "player-self-source-origin.h"

namespace {
thread_local bool round56OriginalSelfNullOrigin = true;
}

bool hasAIRound56EngineSelfNullSourceOrigin() noexcept
{
    // Do not inspect QSanEngine::Self, including for equality with nullptr.
    return round56OriginalSelfNullOrigin;
}

// The engine-side Self. It is declared in the QSAN_ENGINE_BUILD branch of
// src/client/clientplayer.h, but core must not depend back on client headers, so the
// namespace and definition are opened here.
namespace QSanEngine {
thread_local Player *Self = nullptr;
}

void setEngineSelf(Player *player)
{
    // Retire BEFORE the actual source write; null/equal writes also retire.
    round56OriginalSelfNullOrigin = false;
    QSanEngine::Self = player;
}

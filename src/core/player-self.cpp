#include "player.h"

// The engine-side Self. It is declared in the QSAN_ENGINE_BUILD branch of
// src/client/clientplayer.h, but core must not depend back on client headers, so the
// namespace and definition are opened here.
namespace QSanEngine {
Player *Self = nullptr;
}

void setEngineSelf(Player *player)
{
    QSanEngine::Self = player;
}

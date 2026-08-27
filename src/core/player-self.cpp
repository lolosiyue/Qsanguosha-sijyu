#include "player.h"
#include "clientplayer.h"

namespace QSanEngine {
Player *Self = nullptr;
}

void setEngineSelf(Player *player)
{
    QSanEngine::Self = player;
}

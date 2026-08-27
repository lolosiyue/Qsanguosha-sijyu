#include "player.h"

// engine 側的 Self。宣告喺 src/client/clientplayer.h 的 QSAN_ENGINE_BUILD 分支，
// 但 core 不得反向依賴 client header，所以喺呢度自行開 namespace 定義。
namespace QSanEngine {
Player *Self = nullptr;
}

void setEngineSelf(Player *player)
{
    QSanEngine::Self = player;
}

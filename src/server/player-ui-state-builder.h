#ifndef PLAYER_UI_STATE_BUILDER_H
#define PLAYER_UI_STATE_BUILDER_H

#include "protocol/state/player-ui-state.h"

class Room;
class ServerPlayer;

class PlayerUIStateBuilder
{
public:
    static PlayerUIState build(const ServerPlayer &player, const Room &room);
};

#endif

#ifndef TUI_CLIENT_PLAYER_H
#define TUI_CLIENT_PLAYER_H

#include "runtime/client-player-model.h"

// Compatibility name for the terminal frontend. The implementation and the
// ClientPlayer Qt meta-object now live in the shared client runtime target.
using TuiPlayerModel = ClientPlayerModel;

#endif

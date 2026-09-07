#ifndef TUI_ROOM_CONTEXT_H
#define TUI_ROOM_CONTEXT_H

#include "runtime/client-room-context.h"

// Compatibility name for the terminal frontend. The implementation now lives
// in the frontend-neutral client runtime boundary so a future WASM frontend can
// use the same EngineRuntimeContext and live WrappedCard projection.
using TuiRoomContext = ClientRoomContext;

#endif

#ifndef AI_STRATEGY_ROUND56_BOOTSTRAP_CAPTURE_METADATA_H
#define AI_STRATEGY_ROUND56_BOOTSTRAP_CAPTURE_METADATA_H
#include <QString>
class Room;struct lua_State;
// A borrowed live LClosure from the CURRENT protected-entry TValue or its
// directly reachable upvalue graph only; never a cached/detached pointer.
// No Lua stack changes, Lua allocation/execution or userdata conversion.
// Native hash/byte allocations can fail; caller must catch and retire.
bool matchAIRound56PinnedClosureMetadata(Room &,lua_State *,const void *liveClosure,
 const QString &path,int firstLine,int lastLine);
// Source-owned table birth membership only, before mode completion. Does not
// inspect table contents, bind a capture name or grant execution authority.
// Caller must prove the entire original bootstrap graph and exact captures.
bool hasAIRound56BootstrapModeTableOrigin(const Room &,lua_State *,const void *) noexcept;
// Actual Auxiliary loaded mainchunk only, against independent source bytes
// compiled/pinned earlier in the Room VM. Does not borrow Room binding or
// mutate either stack; both runtimes must retain their original generation.
bool matchAIRound56PinnedAuxiliaryRoot(Room &,lua_State *auxiliary,const QString &path);
#endif

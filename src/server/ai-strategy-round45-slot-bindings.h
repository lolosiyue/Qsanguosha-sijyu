#pragma once
#include <QList>
struct lua_State;
class ServerPlayer;class Card;class LuaRuntime;
// Native-only game-VM bridges. No declaration is in the SWIG-exposed surface.
// Push helpers may allocate; callers MUST revalidate self/request after push.
bool pushAIRound45SlotPlayer(lua_State *,const ServerPlayer *);
bool matchAIRound45SlotPlayer(lua_State *,int,const ServerPlayer *);
bool pushAIRound45SlotCard(lua_State *,const Card *);
bool matchAIRound45SlotCard(lua_State *,int,const Card *);
// A private native owner retains the exact mutable QList until userdata GC.
// Only helper-issued, still-live wrappers can be read; arbitrary legacy lists
// are not pointer-decoded. Original list operations still mutate this QList.
bool pushAIRound45SlotTargets(lua_State *,const QList<ServerPlayer *> &);
bool readAIRound45SlotTargets(lua_State *,int,QList<ServerPlayer *> &);
// Normal game VM shutdown/reset, AFTER lua_close; also drains wrappers whose
// metatable was replaced. Never called from the asynchronous observer.
void clearAIRound45SlotBindings(LuaRuntime &);

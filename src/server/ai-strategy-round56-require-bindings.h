#ifndef AI_STRATEGY_ROUND56_REQUIRE_BINDINGS_H
#define AI_STRATEGY_ROUND56_REQUIRE_BINDINGS_H
struct lua_State;
// Native loader/upvalue/searcher identities only. This deliberately does not
// inspect module-cache values or authorize a cached/preloaded module result.
// Caller must prove their complete source ancestry, including all mutations.
extern "C" int luaQsgsStandardRequireBindings(lua_State *,int functionIndex);
#endif

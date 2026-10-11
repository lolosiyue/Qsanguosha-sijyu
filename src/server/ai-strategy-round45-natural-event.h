#ifndef AI_STRATEGY_ROUND45_NATURAL_EVENT_H
#define AI_STRATEGY_ROUND45_NATURAL_EVENT_H
#include <memory>
#include <functional>
class Room; class AI; class ServerPlayer; class QVariant; struct lua_State;
// These scopes authenticate the real native dispatch, not an authored call to
// LuaAI::filterEvent. They are native-only and never serialize frame metadata.
class AIRound45NaturalFilterDispatch {
public:
 AIRound45NaturalFilterDispatch(Room &,AI *,ServerPlayer *,int,const QVariant &);
 ~AIRound45NaturalFilterDispatch();
 AIRound45NaturalFilterDispatch(const AIRound45NaturalFilterDispatch &)=delete;
 AIRound45NaturalFilterDispatch &operator=(const AIRound45NaturalFilterDispatch &)=delete;
private:
 struct Data; std::unique_ptr<Data> d;
 friend struct AIRound45NaturalEventAccess;
};
class AIRound45NaturalFilterEntry {
public:
 // Called after pushCallback + method/event/player/QVariant arguments, before
 // protectedCall; original is the exact outer dispatch reference, copy is the
 // Lua-owned QVariant. No SWIG conversion or Lua function is run by this scope.
 AIRound45NaturalFilterEntry(Room &,AI *,ServerPlayer *,int,const QVariant &original,
                            const QVariant &copy,lua_State *);
 ~AIRound45NaturalFilterEntry();
 AIRound45NaturalFilterEntry(const AIRound45NaturalFilterEntry &)=delete;
 AIRound45NaturalFilterEntry &operator=(const AIRound45NaturalFilterEntry &)=delete;
private:
 struct Data; std::unique_ptr<Data> d;
 friend struct AIRound45NaturalEventAccess;
};
struct AIRound45NaturalFilterProof {
 int event=0;
 // Only syntactic public-prompt class; no card ID, target, mark, or payload.
 // All canonical -1/nonnegative signed-int results share the same graph.
 bool faqiAGFormat=false;
};
// Only valid at the exact pre-protectedCall entry with the four prepared args.
// This does NOT seal a Lua function, callback table, saved slot, or native leaf.
bool readAIRound45NaturalFilterEntry(Room &,lua_State *,int argumentCount,
                                    AIRound45NaturalFilterProof *);
// Pushes the actual callback AI owner self through the shared native binding
// seal, never assuming that event actor and observing AI owner are equal.
bool pushAIRound45NaturalFilterSelf(Room &,lua_State *,int argumentCount);
// Focus uses this extra predicate to bind its own copy/reference to that entry.
bool matchesAIRound45NaturalFilterPayload(Room &,lua_State *,int argumentCount,
                                         const QVariant *,ServerPlayer *);
// Native validators may inspect only this already authenticated event argument.
// The callback is compiled C++, must not invoke Lua, and publishes no payload.
bool inspectAIRound45NaturalFilterEntry(Room &,lua_State *,int argumentCount,
    const std::function<bool(int,ServerPlayer *,const QVariant &)> &inspector);
bool matchesAIRound47NativeFilterDispatch(Room&,lua_State*,int,int,ServerPlayer*,const QVariant*);
// Pre-allocation authentication of the genuine native dispatch only. No Lua
// argument exists yet and no QVariant value is inspected.
bool matchesAIRound56NaturalFilterPreEntry(Room&,AI*,ServerPlayer*,int,const QVariant*,lua_State*) noexcept;
#endif

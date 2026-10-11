#ifndef AI_STRATEGY_ROUND56_CLONE_SCOPE_H
#define AI_STRATEGY_ROUND56_CLONE_SCOPE_H
#include <memory>
class Room; class ServerPlayer; struct lua_State;
// Starts before the CloneAI lookup and first SWIG allocation. Source admission
// is independent of existing priority/aiNoTo cells and never creates a grant.
class AIRound56CloneScope final {
public:
 AIRound56CloneScope(Room&,ServerPlayer*,lua_State*);
 ~AIRound56CloneScope();
 bool accepted() const noexcept;
 AIRound56CloneScope(const AIRound56CloneScope&)=delete;
 AIRound56CloneScope&operator=(const AIRound56CloneScope&)=delete;
private:
 struct Data;std::unique_ptr<Data>d;
 friend bool acceptAIRound56CloneEntry(Room&,lua_State*,int) noexcept;
 friend bool inAIRound56CloneScope(const Room&,lua_State*) noexcept;
};
// Called ONCE by the shared protected-entry observer before priority retirement.
// Only the exact first CloneAI function/argument/depth is preserved. Every
// unexpected nested entry retires this frame before ordinary callbacks run.
bool acceptAIRound56CloneEntry(Room&,lua_State*,int) noexcept;
bool inAIRound56CloneScope(const Room&,lua_State*) noexcept;
#endif

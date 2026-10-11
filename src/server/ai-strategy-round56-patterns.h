#ifndef AI_STRATEGY_ROUND56_PATTERNS_H
#define AI_STRATEGY_ROUND56_PATTERNS_H
#include "lua-runtime.h"
#include <memory>
class Room;class AIRound56BootstrapSourceProof;struct lua_TableObservation;
// Actual original table births and all later writes; no cache-value reseeding.
bool beginAIRound56PatternsLineage(Room&,const LuaRuntime::NativeNamespaceBirth&) noexcept;
bool prepareAIRound56PatternsSources(Room&,lua_State*) noexcept;
bool completeAIRound56PatternsLineage(Room&,lua_State*,const AIRound56BootstrapSourceProof&) noexcept;
bool hasAIRound56PatternsLineage(const Room&,lua_State*) noexcept;
void retireAIRound56PatternsLineage(Room&,lua_State*) noexcept;
void clearAIRound56PatternsLineage(Room&) noexcept;
// Sole native producer: buildCardConversions' existing patterns(true) call.
// Refusal retires optional proof; it never skips/changes the original call.
class AIRound56PatternsScope final {
public:
 AIRound56PatternsScope(Room&,lua_State*) noexcept;
 ~AIRound56PatternsScope();
 AIRound56PatternsScope(const AIRound56PatternsScope&)=delete;
 AIRound56PatternsScope&operator=(const AIRound56PatternsScope&)=delete;
private:
 struct Data;std::unique_ptr<Data>d;
};
bool acceptAIRound56PatternsEntry(Room&,lua_State*,int) noexcept;
// Shared Clone global observer admits only the original equip_patterns writer.
bool allowAIRound56PatternsGlobalWrite(Room&,lua_State*,const lua_TableObservation&) noexcept;
#endif

#ifndef AI_STRATEGY_ROUND56_CLONE_LINEAGE_H
#define AI_STRATEGY_ROUND56_CLONE_LINEAGE_H
#include "lua-runtime.h"
class Room;class AIRound56BootstrapSourceProof;
// Called by the sole native namespace birth observer. The lexical token cannot
// be installed late, replayed, or manufactured from current namespace values.
bool beginAIRound56CloneInputLineage(Room&,const LuaRuntime::NativeNamespaceBirth&) noexcept;
// Completes existing actual-source receipts, never discovers current cache data.
// The issuer calls this before consuming its complete bootstrap-union token.
bool completeAIRound56CloneInputLineage(Room&,lua_State*,const AIRound56BootstrapSourceProof&) noexcept;
bool hasAIRound56CloneInputLineage(const Room&,lua_State*) noexcept;
void clearAIRound56CloneInputLineage(Room&) noexcept;
void retireAIRound56CloneInputLineage(Room&,lua_State*) noexcept;
// Actual empty qlist_cache genesis plus every admitted Clone cache write.
// No current cache lookup or nil sample; unknown writes retire this lineage.
bool hasAIRound56InitialQlistKeyAbsence(const Room&,lua_State*,const QString&key) noexcept;
// Native root identity handoff from actual genesis, never a current table scan.
bool captureAIRound56InitialConnectionRoots(const Room&,lua_State*,const void*&space,const void*&qlist) noexcept;
// Actual CloneAI's pinned qlist_cached publication. The returned identity is
// watched from its source assignment, and remains invalid after any mutation.
const void* captureAIRound56CloneAliveCache(const Room&,lua_State*) noexcept;
#endif

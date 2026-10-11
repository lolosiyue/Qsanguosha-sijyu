#ifndef AI_STRATEGY_ROUND56_PROJECTION_LINEAGE_H
#define AI_STRATEGY_ROUND56_PROJECTION_LINEAGE_H
#include "lua-runtime.h"
class Room;class AIRound56BootstrapSourceProof;struct lua_State;
bool beginAIRound56ProjectionLineage(Room&,const LuaRuntime::NativeNamespaceBirth&) noexcept;
bool prepareAIRound56ProjectionSources(Room&,lua_State*) noexcept;
// Actual source-birth metadata only; never scans a current table or creates state.
bool isAIRound56ProjectionCaptureOrigin(const Room&,lua_State*,const void*) noexcept;
bool completeAIRound56ProjectionLineage(Room&,lua_State*,const AIRound56BootstrapSourceProof&) noexcept;
bool hasAIRound56ProjectionLineage(const Room&,lua_State*) noexcept;
void retireAIRound56ProjectionLineage(Room&,lua_State*) noexcept;
void clearAIRound56ProjectionLineage(Room&) noexcept;
#endif

#ifndef AI_STRATEGY_ROUND45_CORRECTION_PROOF_H
#define AI_STRATEGY_ROUND45_CORRECTION_PROOF_H
#include <QString>
class Room; struct lua_State;
enum class AIRound45CorrectionArguments { DistanceV2, Invalidity };
// Bootstrap-only fixed source compilation: no extension chunk execution.
bool registerAIRound45GetterCorrectionSources(Room&,lua_State*,QString*reason=nullptr);
// Complete finite mark-only subtype. Does not inspect marks, skills, roles,
// ownership or callback return values. Native caller authenticates argument
// types via actual LuaDistanceSkillV2 / LuaInvaliditySkill wrapper signature.
bool proveAIRound45GetterCorrection(Room&,lua_State*,int functionIndex,
 AIRound45CorrectionArguments,QString*reason=nullptr);
// Called only by the central observer before its one protected Lua call.
// Validates the prepared callable and exact SWIG argument surface; never calls
// the policy or reads a player mark/flag/context value. This proves only the
// complete getter body's effects, NOT a Poxi frame, saved slot or whole union.
bool certifyAIRound45GetterCorrectionEntry(Room&,lua_State*,int nargs,
 QString*reason=nullptr);
#endif

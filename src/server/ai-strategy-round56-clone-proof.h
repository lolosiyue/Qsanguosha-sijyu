#ifndef AI_STRATEGY_ROUND56_CLONE_PROOF_H
#define AI_STRATEGY_ROUND56_CLONE_PROOF_H
#include <QString>
class Room; struct lua_State;
// Register audited independent source Protos at trusted startup. No execution.
bool registerAIRound56CloneSources(Room&,lua_State*,QString*reason=nullptr);
// Sync source/provider/capture/effect proof only. Caller MUST additionally prove
// the allocation/finalizer/debug/logging universe before preserving any witness.
// Public domain: 02p, hegemony disabled, no scenario. Never creates provenance.
bool proveAIRound56CloneSource(Room&,lua_State*,QString*reason=nullptr);
#endif

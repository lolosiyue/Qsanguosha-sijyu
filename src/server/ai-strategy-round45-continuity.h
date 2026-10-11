#ifndef AI_STRATEGY_ROUND45_CONTINUITY_H
#define AI_STRATEGY_ROUND45_CONTINUITY_H
#include <QString>
struct lua_State;
class Room;
// Called once at the successful Room bootstrap boundary. Never reinitializes an
// existing generation after gameplay or a callback replacement.
void initializeAIRound45ContinuityCertificate(Room &room);
void clearAIRound45ContinuityCertificate(Room &room);
// Native-only, pre-protectedCall. True means the entire finite code/binding
// graph cannot access an ordinary AI saved-plan slot, regardless of hidden
// branch outcome. It grants no card/payment/state-read capability. False keeps
// the caller's pre-existing unconditional invalidation behavior.
bool certifyAIRound45ContinuityEntry(Room &room, lua_State *state,
                                    int argumentCount, QString *reason = nullptr);
// Native query entry, before private eligibility tests and memo/cache lookups.
// Until the entire current native/Lua correction universe has an unconditional
// effect proof, invalidate all existing observations. No query result changes,
// private values, Lua inspection, allocation or retained-card cleanup here.
void observeAIRound45UnprovedNativeQuery(Room *room,bool preserveAiNoTo=false) noexcept;
#endif

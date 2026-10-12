#ifndef AI_STRATEGY_ROUND47_XUFA_PROOF_H
#define AI_STRATEGY_ROUND47_XUFA_PROOF_H
#include <QString>
#include <QSet>
#include <QList>
class LuaCallbackRef;
class Room;class Card;struct lua_State;
// Xufa-only nonwriter proof. This does not authorize a saved value, a move,
// an event, private knowledge, payment, or preservation of another ledger.
bool registerAIRound47XufaGetterSources(Room&,lua_State*,QString*reason=nullptr);
bool proveAIRound47XufaGetterFunction(Room&,lua_State*,int functionIndex,QString*reason=nullptr);
// Batch A/B source graph inspection only. Caller must prove the COMPLETE
// public provider/native/allocation union before private applicability and
// before installing any exact dispatch membership. This API grants no entry.
bool proveAIRound56ActivateGetterFunction(Room&,lua_State*,int functionIndex,QString*reason=nullptr);
// One lexical, source-only traversal of a complete caller-supplied family.
// Shared nodes are visited once within THIS inspection; nothing is cached or
// admitted between query scopes. Uses the same rows, captures and native seals.
bool proveAIRound56ActivateGetterFamily(Room&,lua_State*,const QList<LuaCallbackRef>&,
    QString*reason=nullptr);
// Separate source domain for a complete nonwriting selection callback body.
// role must match its exact pinned can_select_card/card_selection_feasible/
// can_select_target/targets_feasible descriptor. This is not a native-family,
// request, lifetime or dispatcher certificate and grants no execution entry.
bool proveAIRound56ActivateSelectionFunction(Room&,lua_State*,int functionIndex,
    const char *role,QString*reason=nullptr);
// Metadata of ALL registered correction callbacks, before holder selection.
// No player/card/mark/tag value is evaluated by this routine.
// Binding/Proto inspection only: never sufficient to preserve a ledger.
bool inspectAIRound47XufaCorrectionBindings(Room&,lua_State*,QString*reason=nullptr);
bool collectAIRound56CorrectionCaptureTables(Room&,lua_State*,QSet<const void*>&,QString*reason=nullptr);
bool proveAIRound47XufaCorrectionUnion(Room&,lua_State*,QString*reason=nullptr);
bool certifyAIRound47XufaPatternsEntry(Room&,lua_State*,int nargs,QString*reason=nullptr);
#endif

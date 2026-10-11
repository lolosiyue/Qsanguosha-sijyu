#ifndef AI_STRATEGY_ROUND56_PRIORITY_BOOTSTRAP_H
#define AI_STRATEGY_ROUND56_PRIORITY_BOOTSTRAP_H
#include <QString>
#include <QJsonObject>
#include "lua-runtime.h"
class Room;struct lua_State;class AIRound56BootstrapSourceProof;
// Trusted host integration contract, NOT an independently usable capability:
// begin receives the original native namespace birth only. It creates provisional
// observers; positive completion requires the original full source token. The complete
// fixed bootstrap source/helper/native/async graph is proved. The caller owns
// the loaded-chunk and bootstrap-async observers; every unknown entry MUST
// retire before dispatch. complete is after the entire ordinary loader returns.
// No post-bootstrap call can mint/reseed a source origin, even after retirement.
// begin may throw on native allocation; the sole native-birth issuer must
// catch all exceptions, retire every provisional lineage and never retry.
bool beginAIRound56PriorityBootstrap(Room &,const LuaRuntime::NativeNamespaceBirth &);
bool completeAIRound56PriorityBootstrap(Room &,lua_State *,const AIRound56BootstrapSourceProof &) noexcept;
void retireAIRound56PriorityBootstrap(Room &) noexcept;
void clearAIRound56PriorityBootstrap(Room &) noexcept;
// Internal native identity only, never exposed to authored Lua. This does not
// prove the request, whole sorter, cache ancestry, or projection/async graphs.
bool hasAIRound56InitialPriority(Room &,lua_State *,const QString &) noexcept;
// Call only from the shared current-request priority reader after its complete
// native activation/range/owner proof. Reads the ACTUAL cell after provenance.
QJsonObject readAIRound56InitialPriority(Room &,lua_State *,const QString &);
// Original native namespace absence, retired by EVERY write (including nil).
// This is a source fact for the full ordinary aiConnect graph, not its result.
bool hasAIRound56InitialConnectAbsence(Room &,lua_State *) noexcept;
// Fixed native phase/source-origin diagnostics only; no cell values or authority.
QJsonObject inspectAIRound56PriorityBootstrap(Room &);
#endif

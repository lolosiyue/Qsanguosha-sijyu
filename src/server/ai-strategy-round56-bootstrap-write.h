#ifndef AI_STRATEGY_ROUND56_BOOTSTRAP_WRITE_H
#define AI_STRATEGY_ROUND56_BOOTSTRAP_WRITE_H
#include <QString>
#include "lua.hpp"
class Room;
// A source-time assignment-site witness ONLY. The caller separately requires
// its still-live complete public bootstrap effect certificate, source seals,
// root watches and async union. This function never grants/reseeds a value.
// Namespace writes admit the exact SWIG nonsetter bridge; plain writes admit
// only the VM slow/fast path's current Lua writer. Event and operands must agree.
bool matchAIRound56BootstrapWrite(Room &,lua_State *,const lua_TableObservation &,
                                 const QString &sourcePath,int trustedLine,const char *key,
                                 bool namespaceGenesis,int firstLine=0,int lastLine=0) noexcept;
#endif

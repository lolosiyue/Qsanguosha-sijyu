#ifndef AI_STRATEGY_ROUND56_PLAYLOOP_FILTER_H
#define AI_STRATEGY_ROUND56_PLAYLOOP_FILTER_H
#include "lua-runtime.h"
#include <memory>
class Room;class LuaAI;class ServerPlayer;class QVariant;class AIRound56BootstrapSourceProof;
struct lua_TableObservation;
// Native namespace/source births only. No late current-table adoption.
bool beginAIRound56PlayLoopSource(Room&,const LuaRuntime::NativeNamespaceBirth&) noexcept;
bool prepareAIRound56PlayLoopSource(Room&,lua_State*) noexcept;
bool completeAIRound56PlayLoopSource(Room&,lua_State*,const AIRound56BootstrapSourceProof&) noexcept;
bool hasAIRound56PlayLoopSource(const Room&,lua_State*) noexcept;
void retireAIRound56PlayLoopSource(Room&,lua_State*) noexcept;
void clearAIRound56PlayLoopSource(Room&) noexcept;
// Pure source-time predicate; safe for observers in either registration order.
bool allowAIRound56PlayLoopWrite(Room&,lua_State*,const lua_TableObservation&) noexcept;
// Late connection-ledger creation only: both roots have actual reset births,
// publications and NO intervening nested write. Existing ledgers use their own
// observed reset/write receipts, not this pristine-origin predicate.
bool captureAIRound56PlayLoopCacheOrigin(const Room&,lua_State*,const void*&space,
 const void*&qlist,const void*&connect,quint64&serial) noexcept;
class AIRound56PlayLoopFilterScope final {
public:
 ~AIRound56PlayLoopFilterScope();
 AIRound56PlayLoopFilterScope(const AIRound56PlayLoopFilterScope&)=delete;
 AIRound56PlayLoopFilterScope&operator=(const AIRound56PlayLoopFilterScope&)=delete;
 bool accepted()const noexcept;
private:
 friend class LuaAI;
 friend bool allowAIRound56PlayLoopWrite(Room&,lua_State*,const lua_TableObservation&) noexcept;
 friend bool acceptAIRound56PlayLoopFilterEntry(Room&,lua_State*,int) noexcept;
 friend struct AIRound56PlayLoopAccess;
 // Only compiled LuaAI::filterEvent, before callback/wrapper allocations.
 AIRound56PlayLoopFilterScope(Room&,LuaAI*,ServerPlayer*,int,const QVariant&,lua_State*) noexcept;
 struct Data;std::unique_ptr<Data>d;
};
bool acceptAIRound56PlayLoopFilterEntry(Room&,lua_State*,int) noexcept;
#endif

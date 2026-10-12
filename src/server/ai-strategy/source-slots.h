#ifndef AI_STRATEGY_SOURCE_SLOTS_H
#define AI_STRATEGY_SOURCE_SLOTS_H

#include "lua-runtime.h"
#include <QJsonObject>
#include <QString>
#include <memory>

class AIBootstrapSourceProof;
struct AIRequest;
struct AIWorldView;
struct lua_State;
class Room;
class ServerPlayer;

typedef int (*AIUsePriorityCFunction)(lua_State*);
// Native-only lexical seal. The fixed callers validate operation/card/key;
// nothing here executes a setter or accepts a Lua-provided function/token.
class AIUsePriorityOperation {
public:
 enum Mode { Preserve, LiteralTen, CopyCell, Arithmetic };
 AIUsePriorityOperation(Room&,ServerPlayer*,const AIRequest&,lua_State*,AIUsePriorityCFunction,void*,Mode,const QString&target={},const QString&source={});
 ~AIUsePriorityOperation();
 AIUsePriorityOperation(const AIUsePriorityOperation&)=delete;
 AIUsePriorityOperation&operator=(const AIUsePriorityOperation&)=delete;
 // Called after allocations, immediately before the actual RHS/read or write.
 void beforeWrite();
 // Called immediately after the actual plain-table write; never on a skip.
 void afterWrite();
 struct Data;
private:
 std::unique_ptr<Data>d;
};
void observeAIUsePriorityEntry(Room&,lua_State*,int argumentCount)noexcept;
void clearAIUsePriority(Room&);
QJsonObject readAIUsePriority(Room&,ServerPlayer*,const AIRequest&,const QJsonObject&);

// Trusted host integration contract, NOT an independently usable capability:
// begin receives the original native namespace birth only. It creates provisional
// observers; positive completion requires the original full source token. The complete
// fixed bootstrap source/helper/native/async graph is proved. The caller owns
// the loaded-chunk and bootstrap-async observers; every unknown entry MUST
// retire before dispatch. complete is after the entire ordinary loader returns.
// No post-bootstrap call can mint/reseed a source origin, even after retirement.
// begin may throw on native allocation; the sole native-birth issuer must
// catch all exceptions, retire every provisional lineage and never retry.
bool beginAIPriorityBootstrap(Room &,const LuaRuntime::NativeNamespaceBirth &);
bool completeAIPriorityBootstrap(Room &,lua_State *,const AIBootstrapSourceProof &) noexcept;
void retireAIPriorityBootstrap(Room &) noexcept;
void clearAIPriorityBootstrap(Room &) noexcept;
// Internal native identity only, never exposed to authored Lua. This does not
// prove the request, whole sorter, cache ancestry, or projection/async graphs.
bool hasAIInitialPriority(Room &,lua_State *,const QString &) noexcept;
// Call only from the shared current-request priority reader after its complete
// native activation/range/owner proof. Reads the ACTUAL cell after provenance.
QJsonObject readAIInitialPriority(Room &,lua_State *,const QString &);
// Original native namespace absence, retired by EVERY write (including nil).
// This is a source fact for the full ordinary aiConnect graph, not its result.
bool hasAIInitialConnectAbsence(Room &,lua_State *) noexcept;
// Fixed native phase/source-origin diagnostics only; no cell values or authority.
QJsonObject inspectAIPriorityBootstrap(Room &);

// Two fixed actual-source copies only. No value is returned, no reader permit.
QJsonObject copyAISourceUsePriority(Room&,ServerPlayer*,const AIRequest&,const QJsonObject&);

// Fixed qizhan/Jianying suffixes only; no priority value or reader permit.
QJsonObject incrementAISourceUsePriority(Room&,ServerPlayer*,const AIRequest&,const QJsonObject&);

QJsonObject queryAISourceEffects(Room&,const AIRequest&,ServerPlayer*,const QJsonObject&);
class AISourceEffectPermit final {
public:
 AISourceEffectPermit(const AISourceEffectPermit&)=delete;
 AISourceEffectPermit&operator=(const AISourceEffectPermit&)=delete;
private:
 friend QJsonObject queryAISourceEffects(Room&,const AIRequest&,ServerPlayer*,const QJsonObject&);
 friend QJsonObject queryAICollateralFlag(Room&,const AIRequest&,ServerPlayer*,const QJsonObject&);
 friend bool writeAISourceEffect(Room&,const AIRequest&,ServerPlayer*,ServerPlayer*,const AISourceEffectPermit&,QString*);
 AISourceEffectPermit(const Room&r,const AIRequest&q,const ServerPlayer*p,const ServerPlayer*t,const QString&k,bool m,bool v):room(&r),request(&q),owner(p),target(t),key(k),mark(m),value(v){}
 const Room*room;const AIRequest*request;const ServerPlayer*owner;const ServerPlayer*target;
 QString key;bool mark,value;
};
bool writeAISourceEffect(Room&,const AIRequest&,ServerPlayer*,ServerPlayer*,const AISourceEffectPermit&,QString*error=nullptr);

QJsonObject queryAICurrentSourceSlot(Room&,const AIRequest&,ServerPlayer*,const QString&,const QString&,const QJsonObject&);

// Native-only actual gameplay Self bridge for two fixed target slots. No ledger.
QJsonObject queryAISelfTargetSlot(Room&,const AIRequest&,ServerPlayer*,const QString&,const QString&,const QJsonObject&);

bool acceptAIChoulveTargetWriteEntry(Room&,lua_State*,int) noexcept;

// Capture exactly once before any policy entry. AI VM reset must not recapture.
void initializeAITargetRevises(Room &);
// Mirror every instrumented policy entry. A different viewer invalidates shared
// private strategy continuity; legacy/fallback/external execution invalidates it.
void beginAITargetRevises(Room &, const QString &viewer);
void invalidateAITargetRevises(Room &);
// Fixed source Player property aiNoTo only. Host supplies active native request.
// spec = {kind:"target_revise_property",target:string,op:"read"|"write",value?:bool}
QJsonObject queryAITargetRevises(Room &, const AIRequest &,
    const ServerPlayer *viewer, const QJsonObject &spec);

// Constructor-only baseline, before any policy or restored property writer.
void createdAIAiNoTo(Room&,ServerPlayer*);
void initializeAIAiNoTo(Room&);
void beginAIAiNoTo(Room&,const QString&);
void retireAIAiNoTo(Room&) noexcept;
QJsonObject queryAIAiNoTo(Room&,const AIRequest&,const ServerPlayer*,const QJsonObject&);
class AIAiNoToPermit final {
 friend QJsonObject queryAIAiNoTo(Room&,const AIRequest&,const ServerPlayer*,const QJsonObject&);
 const Room*room;const AIRequest*request;const ServerPlayer*owner;const ServerPlayer*target;bool value;
 AIAiNoToPermit(const Room*r,const AIRequest*q,const ServerPlayer*o,const ServerPlayer*t,bool v):room(r),request(q),owner(o),target(t),value(v){}
public:
 bool matches(const Room&r,const AIRequest&q,const ServerPlayer*o,const ServerPlayer*t,bool v)const{return room==&r&&request==&q&&owner==o&&target==t&&value==v;}
};
// R44 existing certified finite mutation machinery; never restamps request/tickets.
bool writeAIAiNoToProperty(Room&,const AIRequest&,ServerPlayer*,ServerPlayer*,bool,const AIAiNoToPermit&,QString*);
// Native diagnostic only: consumes existing constructor provenance; no property read,
// state creation, witness issuance, or Lua/world-view export.
bool hasAIInitialAiNoToWitness(const Room&,const ServerPlayer*) noexcept;
quint64 observedAIInitialAiNoToReads(const Room&) noexcept;
void recordAIChoulveTargetWrite(Room&) noexcept;
void recordAIChoulveNativeAcceptance(Room&) noexcept;
quint64 observedAIChoulveTargetWrites(const Room&) noexcept;
quint64 observedAIChoulveNativeAcceptances(const Room&) noexcept;

bool initializeAIPingjian(Room&,lua_State*);
void clearAIPingjian(Room&);
void projectAIPingjian(AIWorldView&,Room&,ServerPlayer*);
QJsonObject queryAIPingjianCatalog(Room&,ServerPlayer*,const AIRequest&,const QJsonObject&);

bool registerAICardChoiceSource(Room&,lua_State*);
QJsonObject queryAICardChoicePrefix(Room&,ServerPlayer*,const AIRequest&,const QJsonObject&);

#endif

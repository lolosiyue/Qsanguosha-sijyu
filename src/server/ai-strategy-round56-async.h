#ifndef AI_STRATEGY_ROUND56_ASYNC_H
#define AI_STRATEGY_ROUND56_ASYNC_H
#include <QString>
#include <memory>
#include <QPointer>
struct lua_State;
class Room;

// Native source-proof metadata only. Never exported to Lua or a world view.
// The initializer is called ONCE, after trusted rules/AI bootstrap and before
// players/private gameplay. It cannot revive a retired universe.
class AIRound56BootstrapSourceProof final {
public:
 using Current = bool (*)(Room &,lua_State *,quint64 sourceSerial) noexcept;
 ~AIRound56BootstrapSourceProof() = default;
 AIRound56BootstrapSourceProof(const AIRound56BootstrapSourceProof &) = delete;
 AIRound56BootstrapSourceProof &operator=(const AIRound56BootstrapSourceProof &) = delete;
private:
 friend bool completeAIRound56PriorityBootstrap(Room &,lua_State *,const AIRound56BootstrapSourceProof &) noexcept;
 // Implemented ONLY by the complete fixed bootstrap source-union prover.
 // Its current check validates public source provenance, never private values
 // or the presently pending finalizer population.
 friend bool completeAIRound56ModeEvaluationLineage(Room &,lua_State *,const AIRound56BootstrapSourceProof &) noexcept;
 friend bool completeAIRound56PlayLoopSource(Room &,lua_State *,const AIRound56BootstrapSourceProof &) noexcept;
 friend bool completeAIRound56PatternsLineage(Room &,lua_State *,const AIRound56BootstrapSourceProof &) noexcept;
 friend bool completeAIRound56ProjectionLineage(Room &,lua_State *,const AIRound56BootstrapSourceProof &) noexcept;
 friend bool completeAIRound56CloneInputLineage(Room &,lua_State *,const AIRound56BootstrapSourceProof &) noexcept;
 friend std::unique_ptr<AIRound56BootstrapSourceProof>
 proveAIRound56BootstrapSourceUnion(Room &,lua_State *) noexcept;
 friend bool initializeAIRound56AsyncUniverse(Room &,lua_State *,
                                              AIRound56BootstrapSourceProof &,QString *);
 AIRound56BootstrapSourceProof(Room &r,lua_State *L,quint64 generation,
                               quint64 serial,Current current,bool gameplayVariants);
 QPointer<QObject> m_roomLifetime;
 Room *m_room;lua_State *m_lua;quint64 m_generation,m_serial;
 Current m_current;bool m_gameplayVariants,m_consumed=false;
};
std::unique_ptr<AIRound56BootstrapSourceProof>
proveAIRound56BootstrapSourceUnion(Room &,lua_State *) noexcept;
// Consume a complete pre-selection source-union proof once. Heap metadata is
// checked only defensively after authority exists for every branch/product.
bool initializeAIRound56AsyncUniverse(Room &,lua_State *,
                                     AIRound56BootstrapSourceProof &,QString *why=nullptr);
// Compatibility entry deliberately cannot seed without the source token.
bool initializeAIRound56AsyncUniverse(Room &,lua_State *,QString *why=nullptr);
bool hasAIRound56AsyncUniverse(const Room &, lua_State *) noexcept;
void retireAIRound56AsyncUniverse(lua_State *) noexcept;
// True also on missing/retired state: callers must refuse mutable captures.
bool isAIRound56AsyncProtectedTable(const Room &, lua_State *, int index) noexcept;

// Mandatory caller premise: the complete pinned synchronous effect, capture,
// native-callee, allocation and host signal/logging graph was proved BEFORE
// private selection. This class does not prove that graph for its caller.
// That graph must neither change finalizer/hook/registry capabilities nor
// resume a coroutine, process events, or create an unreviewed owned product.
// FreshLuaAI additionally proves the exact new LuaAI -> self.lua_ai -> callback
// registry rooting sequence, including its pristine-QObject error prefixes.
// GameplayVariants proves the fixed SERVER producer type algebra documented
// in the R56 native variant audit: no foreign native injection, generic
// QMetaType allocation, or binary QVariant ingress into gameplay properties,
// tags or SkillContext/SkillInstance state. Each planned allocation is an exact
// qvariant.i/registered SWIG copy/conversion from that algebra. This premise
// covers ALL payload alternatives before private selection; no payload/type
// observation may establish it. Its destructors only release value storage,
// native leases, or post deferred deletion. Caller graph cannot dispatch events.
class AIRound56AsyncScope final {
public:
 enum Products { Values = 0, FreshLuaAI = 1, GameplayVariants = 2,
                 FreshLuaAIAndGameplayVariants = 3 };
 AIRound56AsyncScope(Room &, lua_State *, Products = Values);
 ~AIRound56AsyncScope();
 bool accepted() const noexcept { return m_active; }
 AIRound56AsyncScope(const AIRound56AsyncScope &) = delete;
 AIRound56AsyncScope &operator=(const AIRound56AsyncScope &) = delete;
private:
 friend bool acceptAIRound56AsyncEntry(lua_State *, int) noexcept;
 friend void observeAIRound56AsyncProtectedEntry(lua_State *) noexcept;
 void *m_state = nullptr;
 AIRound56AsyncScope *m_previous = nullptr;
 lua_State *m_lua = nullptr;
 bool m_active = false;
};

// Called by the sole runtime entry transport. Positive means only that the
// existing source-retirement observers need not run for this exact async
// entry; Lua executes the callback unchanged. No callback is suppressed.
bool acceptAIRound56AsyncEntry(lua_State *, int asyncKind) noexcept;
void observeAIRound56AsyncProtectedEntry(lua_State *) noexcept;

// Verify every registered SWIG allocation metatable's exact __gc source,
// including classes with no currently pending instance. No Lua allocation.
bool aiRound56RegisteredFinalizerUniverse(lua_State *) noexcept;
// Exact compiled SWIG/native-standard-library destructor proof primitives.
// 'function' and 'object' are stack indices, and inspection does not allocate.
bool aiRound56NativeFinalizerNonwriter(lua_State *, int function, int object,
                                     bool freshLuaAIOrigin, bool gameplayVariantOrigin = false) noexcept;
extern "C" int luaQsgsStandardIoFinalizer(lua_State *, int function, int object);
extern "C" int luaQsgsStandardLoadlibFinalizer(lua_State *, int function, int object);
extern "C" int luaQsgsStandardWarningSink(lua_State *);

// Defined beside R37's private hook/capture map. It must match the active
// lexical capture and the exact constructorHook pointer, not just its mask.
bool matchesAIRound56OriginalConstructorHook(lua_State *) noexcept;
#endif

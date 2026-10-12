#ifndef AI_STRATEGY_LUA_SEAL_H
#define AI_STRATEGY_LUA_SEAL_H

#include <QString>
#include <QList>
#include <QStringList>
#include <QJsonObject>
#include <memory>
#include <functional>
#include <QVector>

class AI;
struct AIRequest;
class Card;
class GameRng;
struct lua_State;
class LuaRuntime;
class QVariant;
class Room;
class ServerPlayer;

// Native-only trusted bootstrap API. fixedSha must be a compiled/audited
// constant; callers must never derive it from the currently installed file.
// Compilation is for independent metadata only: the chunk is never executed.
bool registerAIPinnedSource(Room &, lua_State *, const QString &relativePath,
                                  const QByteArray &fixedSha, QString *reason=nullptr);
// No Lua allocation, function execution, _ENV lookup or userdata inspection.
// This proves complete executable Proto identity, NOT upvalue binding/effects.
bool matchAIPinnedFunction(Room &, lua_State *, int functionIndex,
                                 const QString &relativePath, int firstLine,
                                 int lastLine, QString *reason=nullptr);
// Same complete-code check, additionally restricted to descendants of the
// listed independently audited outer function anchors. No line-range admit.
bool matchAIPinnedFunctionFamily(Room &, lua_State *, int functionIndex,
                                       const QString &relativePath, const QList<int> &outerFirstLines,
                                       QString *reason=nullptr);
// Metadata-only source-time witness. borrowedClosure MUST come directly from
// the current live CallInfo; never pass a retained pointer or authored handle.
// The trusted line is looked up in the independently compiled pinned source,
// not current debug metadata. This proves code/PC only, not effects or _ENV.
bool matchAIPinnedInstruction(Room &,lua_State *,const void *borrowedClosure,
                                    int pc,const QString &relativePath,int firstLine,
                                    int lastLine,int trustedSourceLine);
void clearAIPinnedSources(Room &);
// Only the synchronous observer's CURRENT LIVE closure and explicit VM PC,
// or an actual suspended Lua caller PC, may be supplied. No pointer cache.
bool matchAIPinnedLiveInstruction(Room &,lua_State *,const void *borrowedClosure,
 int pc,const QString &path,const QList<int> &families,int requiredOpcode=-1);

// Capture immediately after game LuaRuntime::initialize, before config.lua or
// any authored package. Native-only and one-shot per generation.
void captureAINativeBindings(Room &,lua_State *);
void clearAINativeBindings(Room &);
// Code/binding identity only. Callers supply independently audited finite leaf
// names/effects; these APIs do not classify arbitrary native methods as safe.
bool matchAINativeBinding(Room &,lua_State *,int valueIndex,const QString &path);
bool matchAINativeConstant(Room &,lua_State *,int valueIndex,const QString &path);
bool matchAINativeClassMethod(Room &,lua_State *,const QString &className,const QString &method);
bool matchAINativeDerivedMethods(Room &,lua_State *,const QString &baseClass,const QStringList &methods);
bool matchAINativeNamespaceProperty(Room &,lua_State *,const QString &space,const QString &property);
bool matchAINativeClassProperty(Room &,lua_State *,const QString &className,const QString &property);
bool matchAINativeObjectMethods(Room &,lua_State *,int objectIndex,const QString &baseClass,const QStringList &methods);
bool matchAINativeNamespaceWrite(Room &,lua_State *,const QString &space,const QString &key);
bool matchAINativeFileMethods(Room &,lua_State *,const QStringList &methods);
// Current exact SWIG namespace __newindex frame and nonsetter raw-write route.
// No stack growth/push, userdata conversion, Lua calls, or authored values.
bool matchAINativeNamespaceWriteFrame(Room &,lua_State *,const void *tableIdentity,
                                           const QString &space,const char *key);
// Exact nil lookup, including absence in BOTH SWIG .get and .fn tables.
bool matchAINativeNamespaceRead(Room &,lua_State *,const QString &space,const QString &key);
// R47 fixed scratch factories only; identity captured before configured scripts.
bool matchAINativeScratchConstructor(Room &,lua_State *,int,const QString &);
bool matchAINativeValueMetamethods(Room &,lua_State *,QString *reason=nullptr);
// Trusted bootstrap string metatable identity and every exact entry. This is
// binding evidence only: callers must also prove operand metamethod effects.
bool matchAINativeStringMetatable(Room &,lua_State *);
// R56 native property setter identity, without invocation or value inspection.
bool matchAINativeClassSetter(Room &,lua_State *,const QString &className,const QString &property);
// Exact current native frame against pre-script captured no-upvalue leaf.
// No Lua stack changes or allocations.
bool matchAINativeLeafFrame(Room &,lua_State *,const QString &path);
// Exact current zero-upvalue native class-method frame, captured at birth.
// Binding identity only; caller proves the finite native effect/return typemap.
bool matchAINativeClassLeafFrame(Room&,lua_State*,const QString&className,const QString&method);
// One deterministic original-bootstrap native writer. Begin watches the actual
// native-birth CardUseStruct metatable; complete needs its observed __gc write.
bool beginAILianzhanFinalizerTransition(Room&,lua_State*);
bool completeAILianzhanFinalizerTransition(Room&,lua_State*);

class AIOriginalTurnUseConstructor final {
public:
 AIOriginalTurnUseConstructor(Room&,ServerPlayer*,lua_State*);
 ~AIOriginalTurnUseConstructor();
 void complete(AI*);
 AIOriginalTurnUseConstructor(const AIOriginalTurnUseConstructor&)=delete;
 AIOriginalTurnUseConstructor&operator=(const AIOriginalTurnUseConstructor&)=delete;
private:struct Data;Data*d;
};
void initializeAIOriginalTurnUse(Room&);
void initializeAIChoiceRegistryTables(Room&,lua_State*,bool isolated);
void captureAIChoiceRegistryProxies(Room&,lua_State*);
bool validateAIChoiceRegistryProxies(Room&);
void invalidateAIOriginalTurnUse(Room&);
QJsonObject queryAIOriginalTurnUse(Room&,const AIRequest&,const ServerPlayer*,const QString&write=QString());
// Exact already-installed native constructor hook, valid only in its live lexical capture.
bool matchesAIOriginalConstructorHook(Room&,lua_State*) noexcept;

// Exact capture-owned room lookup for async hook inspection.
bool matchesAIOriginalConstructorHook(lua_State*) noexcept;

// These scopes authenticate the real native dispatch, not an authored call to
// LuaAI::filterEvent. They are native-only and never serialize frame metadata.
class AINaturalFilterDispatch {
public:
 AINaturalFilterDispatch(Room &,AI *,ServerPlayer *,int,const QVariant &);
 ~AINaturalFilterDispatch();
 AINaturalFilterDispatch(const AINaturalFilterDispatch &)=delete;
 AINaturalFilterDispatch &operator=(const AINaturalFilterDispatch &)=delete;
private:
 struct Data; std::unique_ptr<Data> d;
 friend struct AINaturalEventAccess;
};
class AINaturalFilterEntry {
public:
 // Called after pushCallback + method/event/player/QVariant arguments, before
 // protectedCall; original is the exact outer dispatch reference, copy is the
 // Lua-owned QVariant. No SWIG conversion or Lua function is run by this scope.
 AINaturalFilterEntry(Room &,AI *,ServerPlayer *,int,const QVariant &original,
                            const QVariant &copy,lua_State *);
 ~AINaturalFilterEntry();
 AINaturalFilterEntry(const AINaturalFilterEntry &)=delete;
 AINaturalFilterEntry &operator=(const AINaturalFilterEntry &)=delete;
private:
 struct Data; std::unique_ptr<Data> d;
 friend struct AINaturalEventAccess;
};
struct AINaturalFilterProof {
 int event=0;
 // Only syntactic public-prompt class; no card ID, target, mark, or payload.
 // All canonical -1/nonnegative signed-int results share the same graph.
 bool faqiAGFormat=false;
};
// Only valid at the exact pre-protectedCall entry with the four prepared args.
// This does NOT seal a Lua function, callback table, saved slot, or native leaf.
bool readAINaturalFilterEntry(Room &,lua_State *,int argumentCount,
                                    AINaturalFilterProof *);
// Pushes the actual callback AI owner self through the shared native binding
// seal, never assuming that event actor and observing AI owner are equal.
bool pushAINaturalFilterSelf(Room &,lua_State *,int argumentCount);
// Focus uses this extra predicate to bind its own copy/reference to that entry.
bool matchesAINaturalFilterPayload(Room &,lua_State *,int argumentCount,
                                         const QVariant *,ServerPlayer *);
// Native validators may inspect only this already authenticated event argument.
// The callback is compiled C++, must not invoke Lua, and publishes no payload.
bool inspectAINaturalFilterEntry(Room &,lua_State *,int argumentCount,
    const std::function<bool(int,ServerPlayer *,const QVariant &)> &inspector);
bool matchesAINativeFilterDispatch(Room&,lua_State*,int,int,ServerPlayer*,const QVariant*);
// Pre-allocation authentication of the genuine native dispatch only. No Lua
// argument exists yet and no QVariant value is inspected.
bool matchesAINaturalFilterPreEntry(Room&,AI*,ServerPlayer*,int,const QVariant*,lua_State*) noexcept;

// Compile-only source registration at the trusted bootstrap boundary.
bool registerAINaturalFilterSources(Room &,lua_State *,QString *reason=nullptr);
// Whole finite binding/effect graph, never an execution/branch trace. A missing
// native leaf, callback sibling, finalizer premise or family proof fails closed.
bool certifyAINaturalFilterGraph(Room &,lua_State *,int nargs,QString *reason=nullptr);

// Native-only game-VM bridges. No declaration is in the SWIG-exposed surface.
// Push helpers may allocate; callers MUST revalidate self/request after push.
bool pushAISlotPlayer(lua_State *,const ServerPlayer *);
bool matchAISlotPlayer(lua_State *,int,const ServerPlayer *);
bool pushAISlotCard(lua_State *,const Card *);
bool matchAISlotCard(lua_State *,int,const Card *);
// A private native owner retains the exact mutable QList until userdata GC.
// Only helper-issued, still-live wrappers can be read; arbitrary legacy lists
// are not pointer-decoded. Original list operations still mutate this QList.
bool pushAISlotTargets(lua_State *,const QList<ServerPlayer *> &);
bool readAISlotTargets(lua_State *,int,QList<ServerPlayer *> &);
// Normal game VM shutdown/reset, AFTER lua_close; also drains wrappers whose
// metatable was replaced. Never called from the asynchronous observer.
void clearAISlotBindings(LuaRuntime &);

// Invocation-local native state only. No Lua setter or persistent replay API.
class AISourceCompatibilitySession final {
public:
    struct ClockRead { QString key; qint64 seconds; };
    AISourceCompatibilitySession(Room &, const AIRequest &, int nativeTrigger = -1,
                          const QString &nativeEventKind = QString());
    QJsonObject clock(const QString &key);
    QJsonObject gaodaRandom(GameRng &aiRng, int upper);
    QJsonObject numericCosts(const QString &package, const QString &text) const;
    bool replayClock(const QVector<ClockRead> &reads);
    const QVector<ClockRead> &clockReads() const { return m_reads; }
private:
    bool current() const;
    bool gaoda() const;
    Room &m_room;
    const AIRequest &m_request;
    QString m_viewer, m_eventKind;
    quint64 m_decision, m_revision;
    int m_trigger;
    QVector<ClockRead> m_reads, m_replay;
    bool m_replaying = false, m_shuffleReady = false;
    int m_nextUpper = 0;
};

#endif

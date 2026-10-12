#ifndef AI_STRATEGY_RUNTIME_H
#define AI_STRATEGY_RUNTIME_H

#include "engine.h"
#include "lua.hpp"
#include "lua-runtime.h"
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <memory>
#include <QPointer>
#include <QtGlobal>

class AIBootstrapSourceProof;
struct AIRequest;
struct AIWorldView;
class Card;
struct lua_State;
struct lua_TableObservation;
class LuaAI;
class Player;
class QObject;
class QVariant;
class Room;
class RoomDefinitionRegistry;
class ServerPlayer;

// A borrowed live LClosure from the CURRENT protected-entry TValue or its
// directly reachable upvalue graph only; never a cached/detached pointer.
// No Lua stack changes, Lua allocation/execution or userdata conversion.
// Native hash/byte allocations can fail; caller must catch and retire.
bool matchAIPinnedClosureMetadata(Room &,lua_State *,const void *liveClosure,
 const QString &path,int firstLine,int lastLine);
// Source-owned table birth membership only, before mode completion. Does not
// inspect table contents, bind a capture name or grant execution authority.
// Caller must prove the entire original bootstrap graph and exact captures.
bool hasAIBootstrapModeTableOrigin(const Room &,lua_State *,const void *) noexcept;
// Actual Auxiliary loaded mainchunk only, against independent source bytes
// compiled/pinned earlier in the Room VM. Does not borrow Room binding or
// mutate either stack; both runtimes must retain their original generation.
bool matchAIPinnedAuxiliaryRoot(Room &,lua_State *auxiliary,const QString &path);
// Isolated source registration only: executable identity against the already
// pinned Room source, with both live runtime generations checked. No scope grant.
bool matchAIPinnedAuxiliaryFunction(Room &,lua_State *,int functionIndex,
 const QString &path,int firstLine,int lastLine);

// Public native manifest metadata, BEFORE config/rules/private account source.
// Exact frozen loader domain; this neither reads Lua cells nor executes code.
// Config source SHA256: f5d0dcf80699f302b460f655b33372e9266301d9ac8fa2187084ce5c1df9b249
inline bool matchAIBootstrapDeclaredLists(const Engine&engine){
 const QStringList extension_names={
  QStringLiteral("extensions/addFunction.lua"),
  QStringLiteral("extensions/AIgeneral.lua"),
  QStringLiteral("extensions/AnimationCardpack.lua"),
  QStringLiteral("extensions/animecard.lua"),
  QStringLiteral("extensions/animic.lua"),
  QStringLiteral("extensions/arknights.lua"),
  QStringLiteral("extensions/berserk.lua"),
  QStringLiteral("extensions/biaofeng.lua"),
  QStringLiteral("extensions/biaofengold.lua"),
  QStringLiteral("extensions/blood.lua"),
  QStringLiteral("extensions/Christmas.lua"),
  QStringLiteral("extensions/ck.lua"),
  QStringLiteral("extensions/Dan.lua"),
  QStringLiteral("extensions/dmpdiva.lua"),
  QStringLiteral("extensions/dmpkancolle.lua"),
  QStringLiteral("extensions/dmptouhou.lua"),
  QStringLiteral("extensions/dongmanbao.lua"),
  QStringLiteral("extensions/Dragon.lua"),
  QStringLiteral("extensions/DragonLoke.lua"),
  QStringLiteral("extensions/du.lua"),
  QStringLiteral("extensions/emjr.lua"),
  QStringLiteral("extensions/erciyuan.lua"),
  QStringLiteral("extensions/excard2014.lua"),
  QStringLiteral("extensions/extendEffects.lua"),
  QStringLiteral("extensions/extra.lua"),
  QStringLiteral("extensions/fate.lua"),
  QStringLiteral("extensions/fcDIY.lua"),
  QStringLiteral("extensions/feiDIY.lua"),
  QStringLiteral("extensions/gaoda.lua"),
  QStringLiteral("extensions/gaodacard.lua"),
  QStringLiteral("extensions/gaodaexcard.lua"),
  QStringLiteral("extensions/genius.lua"),
  QStringLiteral("extensions/goddess.lua"),
  QStringLiteral("extensions/godExam.lua"),
  QStringLiteral("extensions/gundamboss.lua"),
  QStringLiteral("extensions/htms.lua"),
  QStringLiteral("extensions/hunlie.lua"),
  QStringLiteral("extensions/hunliesp.lua"),
  QStringLiteral("extensions/kearcane.lua"),
  QStringLiteral("extensions/kearjsrg.lua"),
  QStringLiteral("extensions/kearlqxf.lua"),
  QStringLiteral("extensions/kearmsxfy.lua"),
  QStringLiteral("extensions/kearnews.lua"),
  QStringLiteral("extensions/kearwangushengxiang.lua"),
  QStringLiteral("extensions/keguibao.lua"),
  QStringLiteral("extensions/keshashenfuti.lua"),
  QStringLiteral("extensions/keshengbao.lua"),
  QStringLiteral("extensions/kexianbao.lua"),
  QStringLiteral("extensions/keyaobao.lua"),
  QStringLiteral("extensions/kurosakiichigo.lua"),
  QStringLiteral("extensions/legends.lua"),
  QStringLiteral("extensions/leo.lua"),
  QStringLiteral("extensions/lol2015.lua"),
  QStringLiteral("extensions/Loong.lua"),
  QStringLiteral("extensions/lolihime.lua"),
  QStringLiteral("extensions/LuaOldEnemy.lua"),
  QStringLiteral("extensions/LuaOldEnemyGirls.lua"),
  QStringLiteral("extensions/mcompetition.lua"),
  QStringLiteral("extensions/meizl.lua"),
  QStringLiteral("extensions/meow.lua"),
  QStringLiteral("extensions/mojiang.lua"),
  QStringLiteral("extensions/mojiangprevious.lua"),
  QStringLiteral("extensions/moshen.lua"),
  QStringLiteral("extensions/mountainrumour.lua"),
  QStringLiteral("extensions/MR.lua"),
  QStringLiteral("extensions/new.lua"),
  QStringLiteral("extensions/newgenerals.lua"),
  QStringLiteral("extensions/newstar.lua"),
  QStringLiteral("extensions/nMobileEffect.lua"),
  QStringLiteral("extensions/NyarzFirst.lua"),
  QStringLiteral("extensions/NyarzSecond.lua"),
  QStringLiteral("extensions/NyarzThird.lua"),
  QStringLiteral("extensions/nybeauty.lua"),
  QStringLiteral("extensions/offline.lua"),
  QStringLiteral("extensions/olClan.lua"),
  QStringLiteral("extensions/olddiy.lua"),
  QStringLiteral("extensions/OverseasVersion.lua"),
  QStringLiteral("extensions/playtogether.lua"),
  QStringLiteral("extensions/qhstandard.lua"),
  QStringLiteral("extensions/RAFTOM.lua"),
  QStringLiteral("extensions/rushB.lua"),
  QStringLiteral("extensions/scarlet.lua"),
  QStringLiteral("extensions/scarletayuhuo.lua"),
  QStringLiteral("extensions/sgs10th.lua"),
  QStringLiteral("extensions/shadow.lua"),
  QStringLiteral("extensions/shengrongsiai.lua"),
  QStringLiteral("extensions/shixinrumo.lua"),
  QStringLiteral("extensions/sihaigaizao.lua"),
  QStringLiteral("extensions/sijyu.lua"),
  QStringLiteral("extensions/sijyuoffline.lua"),
  QStringLiteral("extensions/sk.lua"),
  QStringLiteral("extensions/SoulA.lua"),
  QStringLiteral("extensions/srkill.lua"),
  QStringLiteral("extensions/sy.lua"),
  QStringLiteral("extensions/symode.lua"),
  QStringLiteral("extensions/touhouproject.lua"),
  QStringLiteral("extensions/typemoon.lua"),
  QStringLiteral("extensions/wolf1411.lua"),
  QStringLiteral("extensions/yongjian.lua"),
  QStringLiteral("extensions/yuri.lua"),
  QStringLiteral("extensions/yy.lua"),
  QStringLiteral("extensions/zabing.lua"),
  QStringLiteral("extensions/zhenghuoCMT.lua"),
  QStringLiteral("extensions/system.lua"),
  QStringLiteral("extensions/birth.lua"),
  QStringLiteral("extensions/cdiy.lua"),
  QStringLiteral("extensions/ctg.lua"),
  QStringLiteral("extensions/game.lua"),
  QStringLiteral("extensions/giantequip.lua"),
  QStringLiteral("extensions/guozhan.lua"),
  QStringLiteral("extensions/happyrebel.lua"),
  QStringLiteral("extensions/hezongkangqin.lua"),
  QStringLiteral("extensions/jiangshi.lua"),
  QStringLiteral("extensions/jieyi.lua"),
  QStringLiteral("extensions/Shijia.lua"),
  QStringLiteral("extensions/xcx.lua"),
  QStringLiteral("extensions/Xiangqi.lua"),
  QStringLiteral("extensions/xiuzheng.lua"),
 };
 if(engine.rulesDeclaredList(QStringLiteral("extension_names"))!=extension_names)return false;
 const QStringList extension_ids={
  QStringLiteral("addFunction"),
  QStringLiteral("AIgeneral"),
  QStringLiteral("AnimationCardpack"),
  QStringLiteral("animecard"),
  QStringLiteral("animic"),
  QStringLiteral("arknights"),
  QStringLiteral("berserk"),
  QStringLiteral("biaofeng"),
  QStringLiteral("biaofengold"),
  QStringLiteral("blood"),
  QStringLiteral("Christmas"),
  QStringLiteral("ck"),
  QStringLiteral("Dan"),
  QStringLiteral("dmpdiva"),
  QStringLiteral("dmpkancolle"),
  QStringLiteral("dmptouhou"),
  QStringLiteral("dongmanbao"),
  QStringLiteral("Dragon"),
  QStringLiteral("DragonLoke"),
  QStringLiteral("du"),
  QStringLiteral("emjr"),
  QStringLiteral("erciyuan"),
  QStringLiteral("excard2014"),
  QStringLiteral("extendEffects"),
  QStringLiteral("extra"),
  QStringLiteral("fate"),
  QStringLiteral("fcDIY"),
  QStringLiteral("feiDIY"),
  QStringLiteral("gaoda"),
  QStringLiteral("gaodacard"),
  QStringLiteral("gaodaexcard"),
  QStringLiteral("genius"),
  QStringLiteral("goddess"),
  QStringLiteral("godExam"),
  QStringLiteral("gundamboss"),
  QStringLiteral("htms"),
  QStringLiteral("hunlie"),
  QStringLiteral("hunliesp"),
  QStringLiteral("kearcane"),
  QStringLiteral("kearjsrg"),
  QStringLiteral("kearlqxf"),
  QStringLiteral("kearmsxfy"),
  QStringLiteral("kearnews"),
  QStringLiteral("kearwangushengxiang"),
  QStringLiteral("keguibao"),
  QStringLiteral("keshashenfuti"),
  QStringLiteral("keshengbao"),
  QStringLiteral("kexianbao"),
  QStringLiteral("keyaobao"),
  QStringLiteral("kurosakiichigo"),
  QStringLiteral("legends"),
  QStringLiteral("leo"),
  QStringLiteral("lol2015"),
  QStringLiteral("Loong"),
  QStringLiteral("lolihime"),
  QStringLiteral("LuaOldEnemy"),
  QStringLiteral("LuaOldEnemyGirls"),
  QStringLiteral("mcompetition"),
  QStringLiteral("meizl"),
  QStringLiteral("meow"),
  QStringLiteral("mojiang"),
  QStringLiteral("mojiangprevious"),
  QStringLiteral("moshen"),
  QStringLiteral("mountainrumour"),
  QStringLiteral("MR"),
  QStringLiteral("new"),
  QStringLiteral("newgenerals"),
  QStringLiteral("newstar"),
  QStringLiteral("nMobileEffect"),
  QStringLiteral("NyarzFirst"),
  QStringLiteral("NyarzSecond"),
  QStringLiteral("NyarzThird"),
  QStringLiteral("nybeauty"),
  QStringLiteral("offline"),
  QStringLiteral("olClan"),
  QStringLiteral("olddiy"),
  QStringLiteral("OverseasVersion"),
  QStringLiteral("playtogether"),
  QStringLiteral("qhstandard"),
  QStringLiteral("RAFTOM"),
  QStringLiteral("rushB"),
  QStringLiteral("scarlet"),
  QStringLiteral("scarletayuhuo"),
  QStringLiteral("sgs10th"),
  QStringLiteral("shadow"),
  QStringLiteral("shengrongsiai"),
  QStringLiteral("shixinrumo"),
  QStringLiteral("sihaigaizao"),
  QStringLiteral("sijyu"),
  QStringLiteral("sijyuoffline"),
  QStringLiteral("sk"),
  QStringLiteral("SoulA"),
  QStringLiteral("srkill"),
  QStringLiteral("sy"),
  QStringLiteral("symode"),
  QStringLiteral("touhouproject"),
  QStringLiteral("typemoon"),
  QStringLiteral("wolf1411"),
  QStringLiteral("yongjian"),
  QStringLiteral("yuri"),
  QStringLiteral("yy"),
  QStringLiteral("zabing"),
  QStringLiteral("zhenghuoCMT"),
  QStringLiteral("system"),
  QStringLiteral("birth"),
  QStringLiteral("cdiy"),
  QStringLiteral("ctg"),
  QStringLiteral("game"),
  QStringLiteral("giantequip"),
  QStringLiteral("guozhan"),
  QStringLiteral("happyrebel"),
  QStringLiteral("hezongkangqin"),
  QStringLiteral("jiangshi"),
  QStringLiteral("jieyi"),
  QStringLiteral("Shijia"),
  QStringLiteral("xcx"),
  QStringLiteral("Xiangqi"),
  QStringLiteral("xiuzheng"),
 };
 if(engine.rulesDeclaredList(QStringLiteral("extension_ids"))!=extension_ids)return false;
 const QStringList package_lua={
 };
 if(engine.rulesDeclaredList(QStringLiteral("package_lua"))!=package_lua)return false;
 const QStringList package_lang={
 };
 if(engine.rulesDeclaredList(QStringLiteral("package_lang"))!=package_lang)return false;
 const QStringList package_ai={
  QStringLiteral("lua/ai/original-hegemony/heg-basara-ai.lua"),
  QStringLiteral("lua/ai/original-hegemony/heg-common-ai.lua"),
  QStringLiteral("lua/ai/original-hegemony/heg-formation-ai.lua"),
  QStringLiteral("lua/ai/original-hegemony/heg-guanxing-ai.lua"),
  QStringLiteral("lua/ai/original-hegemony/heg-loader-ai.lua"),
  QStringLiteral("lua/ai/original-hegemony/heg-lord_ex-ai.lua"),
  QStringLiteral("lua/ai/original-hegemony/heg-maneuvering-ai.lua"),
  QStringLiteral("lua/ai/original-hegemony/heg-manoeuvre-ai.lua"),
  QStringLiteral("lua/ai/original-hegemony/heg-mol-ai.lua"),
  QStringLiteral("lua/ai/original-hegemony/heg-momentum-ai.lua"),
  QStringLiteral("lua/ai/original-hegemony/heg-newsgs-ai.lua"),
  QStringLiteral("lua/ai/original-hegemony/heg-overseas-ai.lua"),
  QStringLiteral("lua/ai/original-hegemony/heg-power-ai.lua"),
  QStringLiteral("lua/ai/original-hegemony/heg-standard-qun-ai.lua"),
  QStringLiteral("lua/ai/original-hegemony/heg-standard-shu-ai.lua"),
  QStringLiteral("lua/ai/original-hegemony/heg-standard-wei-ai.lua"),
  QStringLiteral("lua/ai/original-hegemony/heg-standard-wu-ai.lua"),
  QStringLiteral("lua/ai/original-hegemony/heg-standard_cards-ai.lua"),
  QStringLiteral("lua/ai/original-hegemony/heg-strategic_advantage-ai.lua"),
  QStringLiteral("lua/ai/original-hegemony/heg-transformation-ai.lua"),
 };
 if(engine.rulesDeclaredList(QStringLiteral("package_ai"))!=package_ai)return false;
 return true;
}

// Public native catalogue metadata only. Call for BOTH literal horse families
// before configured Lua or private account selection; this is not a source
// token. Room-created Lua horse products still need their full producer proof.
inline bool matchAIBootstrapHorseFactories(const Engine &engine) {
 return engine.aiStandardCloneFactory(QStringLiteral("OffensiveHorse"))
     && engine.aiStandardCloneFactory(QStringLiteral("DefensiveHorse"));
}

// A source-time assignment-site witness ONLY. The caller separately requires
// its still-live complete public bootstrap effect certificate, source seals,
// root watches and async union. This function never grants/reseeds a value.
// Namespace writes admit the exact SWIG nonsetter bridge; plain writes admit
// only the VM slow/fast path's current Lua writer. Event and operands must agree.
bool matchAIBootstrapWrite(Room &,lua_State *,const lua_TableObservation &,
                                 const QString &sourcePath,int trustedLine,const char *key,
                                 bool namespaceGenesis,int firstLine=0,int lastLine=0) noexcept;

// Native loader/upvalue/searcher identities only. This deliberately does not
// inspect module-cache values or authorize a cached/preloaded module result.
// Caller must prove their complete source ancestry, including all mutations.
extern "C" int luaQsgsStandardRequireBindings(lua_State *,int functionIndex);

// Original RoomRuntime::initialize boundaries only. These functions never
// replace authored execution or make a later snapshot into source authority.
bool installAIBootstrapSourceBoundary(Room &) noexcept;
bool prepareAIBootstrapSourceBoundary(Room &,lua_State *) noexcept;
bool completeAIBootstrapSourceBoundary(Room &,lua_State *) noexcept;
void retireAIBootstrapSourceBoundary(Room &) noexcept;
void clearAIBootstrapSourceBoundary(Room &) noexcept;
// Exact original bootstrap scope only; no post-bootstrap async seed or grant.
bool acceptAIBootstrapAsyncEntry(lua_State *,int asyncKind) noexcept;
bool acceptAIBootstrapProtectedEntry(Room &,lua_State *,int arguments) noexcept;
// Diagnostic fixed labels/public source identity only. No authority capability
// or account/cache/mind/player value is returned.
QJsonObject inspectAIBootstrapSourceBoundary(const Room &);
// Original issuer continuity for recorded native metadata transitions. During
// Completing the one-shot source token is issued but async is not yet seeded;
// after Completed the ordinary Clone/async lineage must still be live.
bool hasAIBootstrapMetadataContinuity(Room&,lua_State*) noexcept;
// Original native definition producer boundary; registry writer observer only.
bool isAIOriginalDefinitionProducer(Room &,const RoomDefinitionRegistry *) noexcept;
bool completeAIFactoryMapOrigin(Room &,lua_State *,const AIBootstrapSourceProof &) noexcept;

// Called once at the successful Room bootstrap boundary. Never reinitializes an
// existing generation after gameplay or a callback replacement.
void initializeAIContinuityCertificate(Room &room);
void clearAIContinuityCertificate(Room &room);
// Native-only, pre-protectedCall. True means the entire finite code/binding
// graph cannot access an ordinary AI saved-plan slot, regardless of hidden
// branch outcome. It grants no card/payment/state-read capability. False keeps
// the caller's pre-existing unconditional invalidation behavior.
bool certifyAIContinuityEntry(Room &room, lua_State *state,
                                    int argumentCount, QString *reason = nullptr);
// Native query entry, before private eligibility tests and memo/cache lookups.
// Until the entire current native/Lua correction universe has an unconditional
// effect proof, invalidate all existing observations. No query result changes,
// private values, Lua inspection, allocation or retained-card cleanup here.
void observeAIUnprovedNativeQuery(Room *room,bool preserveAiNoTo=false) noexcept;

// Native source-proof metadata only. Never exported to Lua or a world view.
// The initializer is called ONCE, after trusted rules/AI bootstrap and before
// players/private gameplay. It cannot revive a retired universe.
class AIBootstrapSourceProof final {
public:
 using Current = bool (*)(Room &,lua_State *,quint64 sourceSerial) noexcept;
 ~AIBootstrapSourceProof() = default;
 AIBootstrapSourceProof(const AIBootstrapSourceProof &) = delete;
 AIBootstrapSourceProof &operator=(const AIBootstrapSourceProof &) = delete;
private:
 friend bool completeAIPriorityBootstrap(Room &,lua_State *,const AIBootstrapSourceProof &) noexcept;
 // Implemented ONLY by the complete fixed bootstrap source-union prover.
 // Its current check validates public source provenance, never private values
 // or the presently pending finalizer population.
 friend bool completeAIModeEvaluationLineage(Room &,lua_State *,const AIBootstrapSourceProof &) noexcept;
 friend bool completeAIPlayLoopSource(Room &,lua_State *,const AIBootstrapSourceProof &) noexcept;
 friend bool completeAICardPatternsLineage(Room &,lua_State *,const AIBootstrapSourceProof &) noexcept;
 friend bool completeAIFactoryMapOrigin(Room &,lua_State *,const AIBootstrapSourceProof &) noexcept;
 friend bool completeAIProjectionLineage(Room &,lua_State *,const AIBootstrapSourceProof &) noexcept;
 friend bool completeAICloneInputLineage(Room &,lua_State *,const AIBootstrapSourceProof &) noexcept;
 friend std::unique_ptr<AIBootstrapSourceProof>
 proveAIBootstrapSourceUnion(Room &,lua_State *) noexcept;
 friend bool initializeAIAsyncUniverse(Room &,lua_State *,
                                              AIBootstrapSourceProof &,QString *);
 AIBootstrapSourceProof(Room &r,lua_State *L,quint64 generation,
                               quint64 serial,Current current,bool gameplayVariants);
 QPointer<QObject> m_roomLifetime;
 Room *m_room;lua_State *m_lua;quint64 m_generation,m_serial;
 Current m_current;bool m_gameplayVariants,m_consumed=false;
};
std::unique_ptr<AIBootstrapSourceProof>
proveAIBootstrapSourceUnion(Room &,lua_State *) noexcept;
// Consume a complete pre-selection source-union proof once. Heap metadata is
// checked only defensively after authority exists for every branch/product.
bool initializeAIAsyncUniverse(Room &,lua_State *,
                                     AIBootstrapSourceProof &,QString *why=nullptr);
// Compatibility entry deliberately cannot seed without the source token.
bool initializeAIAsyncUniverse(Room &,lua_State *,QString *why=nullptr);
bool hasAIAsyncUniverse(const Room &, lua_State *) noexcept;
void retireAIAsyncUniverse(lua_State *) noexcept;
// True also on missing/retired state: callers must refuse mutable captures.
bool isAIAsyncProtectedTable(const Room &, lua_State *, int index) noexcept;

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
class AIAsyncUniverseScope final {
public:
 enum Products { Values = 0, FreshLuaAI = 1, GameplayVariants = 2,
                 FreshLuaAIAndGameplayVariants = 3 };
 AIAsyncUniverseScope(Room &, lua_State *, Products = Values);
 ~AIAsyncUniverseScope();
 bool accepted() const noexcept { return m_active; }
 AIAsyncUniverseScope(const AIAsyncUniverseScope &) = delete;
 AIAsyncUniverseScope &operator=(const AIAsyncUniverseScope &) = delete;
private:
 friend bool acceptAIAsyncEntry(lua_State *, int) noexcept;
 friend void observeAIAsyncProtectedEntry(lua_State *) noexcept;
 void *m_state = nullptr;
 AIAsyncUniverseScope *m_previous = nullptr;
 lua_State *m_lua = nullptr;
 bool m_active = false;
};

// Called by the sole runtime entry transport. Positive means only that the
// existing source-retirement observers need not run for this exact async
// entry; Lua executes the callback unchanged. No callback is suppressed.
bool acceptAIAsyncEntry(lua_State *, int asyncKind) noexcept;
void observeAIAsyncProtectedEntry(lua_State *) noexcept;

// Verify every registered SWIG allocation metatable's exact __gc source,
// including classes with no currently pending instance. No Lua allocation.
bool aiRegisteredFinalizerUniverse(lua_State *) noexcept;
// Exact compiled SWIG/native-standard-library destructor proof primitives.
// 'function' and 'object' are stack indices, and inspection does not allocate.
bool aiNativeFinalizerNonwriter(lua_State *, int function, int object,
                                     bool freshLuaAIOrigin, bool gameplayVariantOrigin = false) noexcept;
extern "C" int luaQsgsStandardIoFinalizer(lua_State *, int function, int object);
extern "C" int luaQsgsStandardLoadlibFinalizer(lua_State *, int function, int object);
extern "C" int luaQsgsStandardWarningSink(lua_State *);

// Defined beside R37's private hook/capture map. It must match the active
// lexical capture and the exact constructorHook pointer, not just its mask.
bool matchesAIOriginalConstructorHook(lua_State *) noexcept;

// Source-owned registration; neither function runs or replaces a callback.
// Default sink attestation belongs at the reviewed native main entry before
// any message handler installation; it is NOT an arbitrary runtime probe.
void initializeAIDefaultMessageSink() noexcept;
void recordAIServerMessageSink(bool installed) noexcept;
quint64 aiServerMessageSinkEpoch() noexcept;
bool proveAIServerLogEffects(const Room &) noexcept;
class AIReviewedLogConnect final {
public:
 explicit AIReviewedLogConnect(const QObject *) noexcept;
 ~AIReviewedLogConnect();
 static bool matches(const QObject *) noexcept;
private:
 const QObject *m_sender;
 AIReviewedLogConnect *m_previous;
};

// Exact ordinary Choulve planner source cache operations. Not a snapshot seed.
QJsonObject queryAIConnectionCache(Room&,const AIRequest&,ServerPlayer*,const QJsonObject&);
bool acceptAIConnectionEntry(Room&,lua_State*,int) noexcept;
bool allowAIConnectionCacheWrite(Room&,lua_State*,const lua_TableObservation&) noexcept;
void clearAIConnectionCache(Room&) noexcept;

// Called by the sole native namespace birth observer. The lexical token cannot
// be installed late, replayed, or manufactured from current namespace values.
bool beginAICloneInputLineage(Room&,const LuaRuntime::NativeNamespaceBirth&) noexcept;
// Completes existing actual-source receipts, never discovers current cache data.
// The issuer calls this before consuming its complete bootstrap-union token.
bool completeAICloneInputLineage(Room&,lua_State*,const AIBootstrapSourceProof&) noexcept;
bool hasAICloneInputLineage(const Room&,lua_State*) noexcept;
void clearAICloneInputLineage(Room&) noexcept;
void retireAICloneInputLineage(Room&,lua_State*) noexcept;
// Actual empty qlist_cache genesis plus every admitted Clone cache write.
// No current cache lookup or nil sample; unknown writes retire this lineage.
bool hasAIInitialQlistKeyAbsence(const Room&,lua_State*,const QString&key) noexcept;
// Native root identity handoff from actual genesis, never a current table scan.
bool captureAIInitialConnectionRoots(const Room&,lua_State*,const void*&space,const void*&qlist) noexcept;
// Actual CloneAI's pinned qlist_cached publication. The returned identity is
// watched from its source assignment, and remains invalid after any mutation.
const void* captureAICloneAliveCache(const Room&,lua_State*) noexcept;

// Register audited independent source Protos at trusted startup. No execution.
bool registerAICloneSources(Room&,lua_State*,QString*reason=nullptr);
// Sync source/provider/capture/effect proof only. Caller MUST additionally prove
// the allocation/finalizer/debug/logging universe before preserving any witness.
// Public domain: 02p, hegemony disabled, no scenario. Never creates provenance.
bool proveAICloneSource(Room&,lua_State*,QString*reason=nullptr);

// Starts before the CloneAI lookup and first SWIG allocation. Source admission
// is independent of existing priority/aiNoTo cells and never creates a grant.
class AICloneScope final {
public:
 AICloneScope(Room&,ServerPlayer*,lua_State*);
 ~AICloneScope();
 bool accepted() const noexcept;
 AICloneScope(const AICloneScope&)=delete;
 AICloneScope&operator=(const AICloneScope&)=delete;
private:
 struct Data;std::unique_ptr<Data>d;
 friend bool acceptAICloneEntry(Room&,lua_State*,int) noexcept;
 friend bool inAICloneScope(const Room&,lua_State*) noexcept;
};
// Called ONCE by the shared protected-entry observer before priority retirement.
// Only the exact first CloneAI function/argument/depth is preserved. Every
// unexpected nested entry retires this frame before ordinary callbacks run.
bool acceptAICloneEntry(Room&,lua_State*,int) noexcept;
bool inAICloneScope(const Room&,lua_State*) noexcept;

// Native namespace birth only: registers empty-table observations, no compile.
bool beginAIModeEvaluationLineage(Room&,const LuaRuntime::NativeNamespaceBirth&) noexcept;
// Original post-Binding/native-seal pre-config boundary only; once, no reseed.
bool prepareAIModeEvaluationSources(Room&,lua_State*) noexcept;
// Called at the original bootstrap completion before consuming its token.
// The issuer additionally proves no mode evaluate/prepare/commit call during
// bootstrap, and that only pinned register/install functions can reach captures.
bool completeAIModeEvaluationLineage(Room&,lua_State*,const AIBootstrapSourceProof&) noexcept;
bool hasAIModeEvaluationLineage(const Room&,lua_State*) noexcept;
bool proveAIModeEvaluationUnion(Room&,lua_State*,QString*why=nullptr) noexcept;
void retireAIModeEvaluationLineage(Room&,lua_State*) noexcept;
void clearAIModeEvaluationLineage(Room&) noexcept;
// Lexical native evaluateModePolicy or ordinary queryStandardMode, BEFORE registry guard
// and pushAIWorldView allocations. publicMode is AIWorldView.modeId. The fixed
// producer builds a fresh plain primitive graph: world,bool for evaluation;
// world,target-string for ordinary objective prepare; world,nil/nonnegative
// integer for the independent Activate relations refresh; then its actual token.
class AIModeEvaluationScope final {
public:
 enum Entry { Evaluation, StandardObjective, StandardRelations };
 AIModeEvaluationScope(Room&,lua_State*,const QString&publicMode,Entry=Evaluation);
 // Original prepare return only; records actual born result/token identities.
 // Refusal retires optional proof without changing native schema/error behavior.
 void preparedObjective(int resultIndex,int tokenIndex) noexcept;
 ~AIModeEvaluationScope();bool accepted()const noexcept;
 AIModeEvaluationScope(const AIModeEvaluationScope&)=delete;
 AIModeEvaluationScope&operator=(const AIModeEvaluationScope&)=delete;
private:
 friend class AIModeWorldSource;
 friend bool acceptAIModeEvaluationEntry(Room&,lua_State*,int) noexcept;
 struct Data;std::unique_ptr<Data>d;
};
// Lexical evaluateModePolicy/queryStandardMode pushAIWorldView call only. This
// native producer scope ends before protectedCall; frame/depth must stay exact.
class AIModeWorldSource final {
public:
 AIModeWorldSource(Room&,lua_State*);~AIModeWorldSource();
 AIModeWorldSource(const AIModeWorldSource&)=delete;
 AIModeWorldSource&operator=(const AIModeWorldSource&)=delete;
private:struct Data;std::unique_ptr<Data>d;
};
bool acceptAIModeEvaluationEntry(Room&,lua_State*,int argumentCount) noexcept;

// Native namespace/source births only. No late current-table adoption.
bool beginAIPlayLoopSource(Room&,const LuaRuntime::NativeNamespaceBirth&) noexcept;
bool prepareAIPlayLoopSource(Room&,lua_State*) noexcept;
bool completeAIPlayLoopSource(Room&,lua_State*,const AIBootstrapSourceProof&) noexcept;
bool hasAIPlayLoopSource(const Room&,lua_State*) noexcept;
void retireAIPlayLoopSource(Room&,lua_State*) noexcept;
void clearAIPlayLoopSource(Room&) noexcept;
// Pure source-time predicate; safe for observers in either registration order.
bool allowAIPlayLoopWrite(Room&,lua_State*,const lua_TableObservation&) noexcept;
// Late connection-ledger creation only: both roots have actual reset births,
// publications and NO intervening nested write. Existing ledgers use their own
// observed reset/write receipts, not this pristine-origin predicate.
bool captureAIPlayLoopCacheOrigin(const Room&,lua_State*,const void*&space,
 const void*&qlist,const void*&connect,quint64&serial) noexcept;
class AIPlayLoopFilterScope final {
public:
 ~AIPlayLoopFilterScope();
 AIPlayLoopFilterScope(const AIPlayLoopFilterScope&)=delete;
 AIPlayLoopFilterScope&operator=(const AIPlayLoopFilterScope&)=delete;
 bool accepted()const noexcept;
private:
 friend class LuaAI;
 friend bool allowAIPlayLoopWrite(Room&,lua_State*,const lua_TableObservation&) noexcept;
 friend bool acceptAIPlayLoopFilterEntry(Room&,lua_State*,int) noexcept;
 friend struct AIPlayLoopAccess;
 // Only compiled LuaAI::filterEvent, before callback/wrapper allocations.
 AIPlayLoopFilterScope(Room&,LuaAI*,ServerPlayer*,int,const QVariant&,lua_State*) noexcept;
 struct Data;std::unique_ptr<Data>d;
};
bool acceptAIPlayLoopFilterEntry(Room&,lua_State*,int) noexcept;

bool beginAIProjectionLineage(Room&,const LuaRuntime::NativeNamespaceBirth&) noexcept;
bool prepareAIProjectionSources(Room&,lua_State*) noexcept;
// Actual source-birth metadata only; never scans a current table or creates state.
bool isAIProjectionCaptureOrigin(const Room&,lua_State*,const void*) noexcept;
bool completeAIProjectionLineage(Room&,lua_State*,const AIBootstrapSourceProof&) noexcept;
bool hasAIProjectionLineage(const Room&,lua_State*) noexcept;
void retireAIProjectionLineage(Room&,lua_State*) noexcept;
void clearAIProjectionLineage(Room&) noexcept;

// Ordinary PlayerChosen/CardChosen full world projection only. Activate's
// buildSkillActionContext and arbitrary view-as callbacks require another proof.
class AIProjectionSourceScope final {
public:
 AIProjectionSourceScope(Room&,ServerPlayer*,lua_State*,QString*why=nullptr);
 ~AIProjectionSourceScope();bool accepted()const noexcept;
private:
 struct Data;std::unique_ptr<Data>d;
 friend bool inAIProjectionSourceScope(const Room&,lua_State*)noexcept;
 friend bool acceptAIProjectionGetterEntry(Room&,lua_State*,int)noexcept;
 friend bool preserveAIProjectionNativeQuery(Room&,const Card*,bool,bool)noexcept;
 friend void retireAIProjectionSource(Room&,lua_State*)noexcept;
 friend void recordAIProjectionSourceCard(Room&,const Card*,const QString&)noexcept;
 friend void recordAIProjectionOwnerSourceCard(Room&,const Player*,const Card*,const QString&)noexcept;
};
bool inAIProjectionSourceScope(const Room&,lua_State*) noexcept;
// Shared dispatcher tries every complete exact source certifier before default
// retirement; a nonmatching sibling thunk is not immediately an unknown entry.
bool acceptAIProjectionGetterEntry(Room&,lua_State*,int) noexcept;
void retireAIProjectionSource(Room&,lua_State*) noexcept;
bool preserveAIProjectionNativeQuery(Room&,const Card*,bool hasCard,bool scalarCardOnly=false) noexcept;
// Source-owned callsites only: fixed named projection/record constructor or the
// intrinsic Player::canSlash new Slash source. Never called from a query result.
void recordAIProjectionSourceCard(Room&,const Card*,const QString&literalName) noexcept;
// Immediate native constructor receipts with the active projection viewer as
// source owner. Only fixed producer callsites may use this; never parsed tags
// or a later selected card/unchanged pointer observation.
void recordAIProjectionOwnerSourceCard(Room&,const Player*,const Card*,const QString&literalName) noexcept;
// Source type membership only: not custody or an execution grant.
bool aiOrdinaryCardPolicyType(const Card *) noexcept;

// Only the two audited native copy sites may instantiate this lexical alias:
// runAnswer's legacy-hook hint and resolveConversionProposal's readonly Play.
// Lua cannot register aliases or provide a certificate.
class AIRangeRequestAlias final {
public:
    AIRangeRequestAlias(const AIRequest &original,const AIRequest &copy);
    ~AIRangeRequestAlias();
    AIRangeRequestAlias(const AIRangeRequestAlias&)=delete;
    AIRangeRequestAlias&operator=(const AIRangeRequestAlias&)=delete;
    static const AIRequest *canonical(const AIRequest &);
    static const AIWorldView *canonical(const AIWorldView &);
private:
    const AIRequest *m_original,*m_copy;
    AIRangeRequestAlias *m_previous;
    static thread_local AIRangeRequestAlias *s_current;
};
bool aiRangeWorldCurrent(Room &,const AIWorldView &);
// No JSON/Lua certificate, request restamp, new action ticket or payment right.
void beginAIRangeDecision(Room &, const AIRequest &, const void *luaRequest = nullptr);
bool aiRangeLuaRequestCurrent(Room &, const AIRequest &, const void *);
void finishAIRangeDecision(Room &, const AIRequest &);
void invalidateAIRangeDecision(Room &);
bool aiRangeRequestCurrent(Room &, const AIRequest &, const ServerPlayer * = nullptr);
// Active native policy session only. Unlike RequestCurrent, this denies
// finished certificates retained solely for final native result admission.
bool aiRangeDecisionActive(Room &, const AIRequest &, const ServerPlayer * = nullptr);
bool aiRangeContinuation(Room &, const AIRequest &);
bool aiRangeSource(const QString &flag, const QString &source);
// Runtime-only call at the original reached setFlags sites, never a generic host setter.
bool writeAIRangeFlag(Room &, const AIRequest &, ServerPlayer *,
    const QString &flag, const QString &source, bool value, QString *error = nullptr);

// Actual original table births and all later writes; no cache-value reseeding.
bool beginAICardPatternsLineage(Room&,const LuaRuntime::NativeNamespaceBirth&) noexcept;
bool prepareAICardPatternsSources(Room&,lua_State*) noexcept;
bool completeAICardPatternsLineage(Room&,lua_State*,const AIBootstrapSourceProof&) noexcept;
bool hasAICardPatternsLineage(const Room&,lua_State*) noexcept;
void retireAICardPatternsLineage(Room&,lua_State*) noexcept;
void clearAICardPatternsLineage(Room&) noexcept;
// Sole native producer: buildCardConversions' existing patterns(true) call.
// Refusal retires optional proof; it never skips/changes the original call.
class AICardPatternsScope final {
public:
 AICardPatternsScope(Room&,lua_State*) noexcept;
 ~AICardPatternsScope();
 AICardPatternsScope(const AICardPatternsScope&)=delete;
 AICardPatternsScope&operator=(const AICardPatternsScope&)=delete;
private:
 struct Data;std::unique_ptr<Data>d;
};
bool acceptAICardPatternsEntry(Room&,lua_State*,int) noexcept;
// Shared Clone global observer admits only the original equip_patterns writer.
bool allowAICardPatternsGlobalWrite(Room&,lua_State*,const lua_TableObservation&) noexcept;

#endif

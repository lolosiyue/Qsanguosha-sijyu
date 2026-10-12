#ifndef AI_STRATEGY_NATIVE_REGISTRY_H
#define AI_STRATEGY_NATIVE_REGISTRY_H

#include <QString>
#include <QVector>
#include <QtGlobal>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <typeinfo>
#include <QStringList>
#include <memory>
#include <functional>

struct AICardView;
struct AIRequest;
struct AIWorldView;
class Analeptic;
class Card;
class Engine;
struct JudgeStruct;
struct lua_State;
class Player;
class QJsonObject;
class QString;
class QVariant;
class Room;
class RoomDefinitionRegistry;
class ServerPlayer;
class Skill;

// These are diagnostic/source obligations, NOT execution capabilities. In
// particular GetterClosureProved does not certify its native descendants.
enum class AIActivateSourceStatus {
    Uncovered, GetterClosureProved, ScalarMethodBodyProved, NoAuthoredCallback,
    CataloguedType, ReadOnlySelectionClosureProved, FactorySourceRecipeClassified
};
struct AIActivateSourceRow {
    QString publicPath;
    QString exactType;
    QString role;
    QString source;
    QString detail;
    int nargs = -1;
    quintptr definitionIdentity = 0;
    quintptr closureIdentity = 0;
    AIActivateSourceStatus status = AIActivateSourceStatus::Uncovered;
};
struct AIActivatePublicSourceReport {
    // True means the public enumeration finished in one live definition frame.
    // It NEVER means the request, source, callback or allocation union is proved.
    bool enumerationComplete = false;
    quint64 generation = 0;
    quint64 definitionVersion = 0;
    QVector<AIActivateSourceRow> rows;
    QString firstUncoveredSource() const;
};

// Enumerates every public getSkillNames definition, Trigger-owned view-as
// component, source-ref Amount definition, Pindian definition and engine card.
// No player/owner selection, applicability test, callback execution or factory
// invocation occurs. Unknown exact types/roles/sources remain Uncovered.
// Caller must hold the ordinary runtime binding on its owner thread.
// No scope/token, dispatcher member set, writer exception, preservation or
// allocation capability is created, even if every currently implemented row
// happens to pass. The remaining global obligations are explicit rows.
AIActivatePublicSourceReport inspectAIActivatePublicSources(
    Room &, lua_State *);

// Keep all existing domain bits unchanged. Additional source metadata uses a
// separate exact-type map and can never upgrade a correction/metadata bit.
class AINativeTypes : public std::unordered_map<std::type_index,unsigned> {
public:
    std::unordered_map<std::type_index,unsigned> activateSourceIds;
    template<class T> void recordActivateSource(unsigned id) {
        activateSourceIds.emplace(std::type_index(typeid(T)), id);
    }
};
#define AI_NATIVE_REQUIRE_DECLARATION(Type, Method, Owner) \
    static_assert(std::is_same<decltype(&Type::Method), decltype(&Owner::Method)>::value, \
        "R56 source method declaring class changed: " #Type "::" #Method)
// Exact compiled type registration. Bit1: complete correction-family type;
// bit2: the reviewed frequency/equipment/quota virtual implementation union.
// Bit4: exact native Pindian prohibition body, requiring the independent
// complete public metadata/correction union for its nested Player queries.
// No bit proves captured Lua callbacks, allocation or host descendants.
unsigned aiNativeEffectClass(const Skill*);
struct AIActivateNativeMethodSource {
    const char *method;
    const char *source;
    bool scalarBodyProved; // BODY ONLY; no caller, lineage or execution grant
    // Separate Xufa notification body domain. Shushen reads the public native
    // Config.EnableHegemony bool and returns 1/2 on BOTH branches; no authored
    // call, allocation, mutation or private receiver selection. This does not
    // promote that configuration-dependent body into Activate scalar proof.
    bool notificationBodyProved = false;
};
struct AIActivateNativeTypeSource {
    const char *typeSpelling; // diagnostic, never used for lookup/authorization
    const char *registrationFile;
    const AIActivateNativeMethodSource *methods;
    unsigned methodCount;
};
const AIActivateNativeTypeSource *aiActivateNativeTypeSource(const Skill *);

// Source-only classification, NOT a runtime policy grant. Call at actual birth /
// adoption source boundaries, or on the complete PUBLIC definition catalogue.
// Never use a selected live card type in place of a missing ancestry receipt.
// 1: local makeAICardView scalar virtual implementations are source-reviewed.
// 2: targetFixed is a complete readonly source family.
// 4: additionally require continuous hasAIEngineSelfNullSourceOrigin().
// 8: WrappedCard delegation; must compose its independently tracked inner birth.
// Scalar recursion/material/change-card edges and lifetime custody remain
// separate obligations. This does not prove constructors, policies or callbacks.
// Unknown types return zero. Abstract entries are harmless source declarations.
unsigned aiCardMetadataSourceEffects(const std::type_info &) noexcept;

// Native-only source facts; no userdata, prototype pointer or dispatch token.
struct AIFactorySourceRow {
    QString publicPath, exactType, source;
    // True only for the closed, exact LuaSkillCard::clone constructor/copy
    // source recipe. It does NOT certify its effects or product lifetime.
    bool luaSkillCloneRecipe = false;
};
struct AIEquipmentFactorySourceRow {
    QString publicName, exactType, reason;
    bool recipe = false, roomLocal = false, nullResult = false;
};
struct AIFactorySourceReport {
    bool enumerationComplete = false;
    bool globalMapOrigin = false, roomMapOrigin = false;
    bool equipmentRecipeFamilyComplete = false;
    // Fixed public ZunweiCard native metaobject alternatives, no product custody.
    bool zunweiSkillRecipeComplete = false;
    bool globalHorseCallbackSourceComplete = false;
    int globalHorseOriginalNullCallbacks = 0;
    QVector<AIEquipmentFactorySourceRow> equipmentRows;
    bool luaSkillCloneSourceFamilyComplete = false;
    QVector<AIFactorySourceRow> rows;
    QString firstUncoveredLuaSkillCloneSource;
    QStringList remainingEffectObligations;
};
AIFactorySourceReport inspectAIPublicFactorySources(Room &);
// Strict complete public global+runtime LuaSkillCard prototype family check.
// No selected name/card/owner parameter. Unknown RTTI/null/invalid frame rejects.
// Success proves source dispatch/constructor/copy recipe membership ONLY;
// every effect obligation in the report is still required before any execution.
// This API cannot authorize Activate, allocation, preservation or a writer.
bool proveAILuaSkillCloneSourceFamily(Room &, QString *reason = nullptr);
// Internal cross-TU enumerator; never executes Parse, clone, metaObject or callback.
void appendAILuaSkillFactorySources(const RoomDefinitionRegistry &,
    AIFactorySourceReport &);
// History check only. Unknown global registration attempts retire permanently.
bool hasAIGlobalFactoryMapOrigin(const Engine *) noexcept;

// Native-only effect proofs. They never inspect a player's ownership/state.
bool aiPublicHasSkill(const QString&name,bool compound=false) noexcept;
bool aiPublicHasSkill(const Skill*,bool includeLose=false) noexcept;
bool aiPublicIsSkillInvalid(const QString&name) noexcept;
bool aiPublicIsSkillInvalid(const Skill*) noexcept;

enum class AIGetterCorrectionArguments { DistanceV2, Invalidity };
// Bootstrap-only fixed source compilation: no extension chunk execution.
bool registerAIGetterCorrectionSources(Room&,lua_State*,QString*reason=nullptr);
// Complete finite mark-only subtype. Does not inspect marks, skills, roles,
// ownership or callback return values. Native caller authenticates argument
// types via actual LuaDistanceSkillV2 / LuaInvaliditySkill wrapper signature.
bool proveAIGetterCorrection(Room&,lua_State*,int functionIndex,
 AIGetterCorrectionArguments,QString*reason=nullptr);
// Called only by the central observer before its one protected Lua call.
// Validates the prepared callable and exact SWIG argument surface; never calls
// the policy or reads a player mark/flag/context value. This proves only the
// complete getter body's effects, NOT a Poxi frame, saved slot or whole union.
bool certifyAIGetterCorrectionEntry(Room&,lua_State*,int nargs,
 QString*reason=nullptr);

// Separate exact MaxCards and AttackRange lexical roots share their complete
// transitive correction dependency preflight. Neither grants full Card policy
// or full Activate. Each begins before its root's private applicability.
class AIMaxCardsScope final {
public:
 AIMaxCardsScope(Room &,ServerPlayer *,lua_State *);
 ~AIMaxCardsScope();
 bool accepted() const noexcept;
 AIMaxCardsScope(const AIMaxCardsScope &)=delete;
 AIMaxCardsScope &operator=(const AIMaxCardsScope &)=delete;
private:
 enum Root { MaxCardsFixedExtra, AttackRangeFixedExtra, OwnerCardRules, PublicDistances, OwnerSlashWithoutCrossbow, OwnerAnaleptic, FixedSlashProbes, FixedOwnerEvaluations, OwnerPhysicalCandidates, ConnectionQuery, PlayerSkillValidity, PublicSkillQueries, OwnerCardState, WorldSkillQueries, OwnerJilei, OwnerPindian, PublicAttackRangeMatrix, OwnerEquipmentQueries, ZunweiSeed };
 AIMaxCardsScope(Room &,ServerPlayer *,lua_State *,Root);
 bool prepareLiteralFactory(const QString &) noexcept;
 friend class AIAttackRangeScope;
 friend class AIOwnerCardRulesScope;
 friend class AIDistanceScope;
 friend class AICardRulesCloneBirth;
 friend class AIOwnerSlashScope;
 friend class AIPlayerSlashBirth;
 friend class AIOwnerAnalepticScope;
 friend class AIOwnerAnalepticBirth;
 friend class AIFixedSlashProbesScope;
 friend class AIOwnerEvaluationsScope;
 friend class AIOwnerCandidatesScope;
 friend class AIConnectionQueryScope;
 friend class AISkillValidityScope;
 friend class AIPublicSkillQueriesScope;
 friend class AIOwnerCardStateScope;
 friend class AIWorldSkillQueriesScope;
 friend class AIOwnerJileiScope;
 friend class AIOwnerPindianScope;
 friend class AIPublicAttackRangeMatrixScope;
 friend class AIOwnerEquipmentQueriesScope;
 friend class AIZunweiSeedScope;
 friend class AIZunweiSkillCloneBirth;
 friend bool inAIConnectionQueryScope(const Room &,lua_State *) noexcept;
 friend class AIAnalepticNullBirth;
 struct Data;std::unique_ptr<Data>d;
 friend bool acceptAIPlayerCorrectionGetterEntry(Room &,lua_State *,int) noexcept;
 friend bool preserveAIPlayerCorrectionNativeQuery(Room &,const Card *,bool) noexcept;
};
bool acceptAIPlayerCorrectionGetterEntry(Room &,lua_State *,int) noexcept;
bool preserveAIPlayerCorrectionNativeQuery(Room &,const Card *,bool hasCard) noexcept;
// Exact native validity queries in UI and action/conversion projection, with
// complete public metadata and transitive correction graph. Each query ends
// before usage/activation providers, publication, Card policy or its consumer.
class AISkillValidityScope final {
 friend class PlayerUIStateBuilder;
 friend class AiDecisionCoordinator;
 AISkillValidityScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIMaxCardsScope::PlayerSkillValidity){}
 AIMaxCardsScope scope;
};
// Original public-board armor blocking and visible Skill metadata loops only.
// Complete definition+correction graph; no Card policy or full board grant.
class AIPublicSkillQueriesScope final {
 friend class AiDecisionCoordinator;
 AIPublicSkillQueriesScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIMaxCardsScope::PublicSkillQueries){}
 AIMaxCardsScope scope;
};
// Original owner physical-state projection only. The full correction graph and
// canonical physical custody precede each original physical-state loop; no
// factory product is issued. Each consumer has its own lexical lifetime.
class AIOwnerCardStateScope final {
 friend class AiDecisionCoordinator;
 friend void projectAIStrategyOwner(AIWorldView &,Room &,ServerPlayer *);
 AIOwnerCardStateScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIMaxCardsScope::OwnerCardState){}
 AIMaxCardsScope scope;
};
// Original world Skill presence queries only; independent complete metadata
// and correction preflight, ending before providers and conditional bodies.
class AIWorldSkillQueriesScope final {
 friend class AiDecisionCoordinator;
 friend void projectAIStrategyOwner(AIWorldView &,Room &,ServerPlayer *);
 friend void projectAIStrategyAnimecardCatalog(AIWorldView &,Room &,const ServerPlayer *);
 friend void projectAIStrategyYjcm2012(AIWorldView &,Room &,ServerPlayer *);
 friend QJsonObject projectAIOwnerFilteredCard(Room &,ServerPlayer *,int,const JudgeStruct *,int,const AICardView *);
 friend void projectAIStrategyResponseContext(AIRequest &,Room &,ServerPlayer *,const QVariant &,QJsonObject &,const std::function<QJsonObject(const Card *)> &);
 friend void projectAIMeowLiuliChoiceContext(AIRequest &,Room &,ServerPlayer *,QJsonObject &,const std::function<QJsonObject(const Card *)> &);
 friend void projectAISRkillChoiceContext(AIRequest &,Room &,ServerPlayer *,const QVariant &,QJsonObject &,const std::function<QJsonObject(const Card *)> &);
 friend void projectAIOverseasChoiceContext(AIRequest &,Room &,ServerPlayer *,const QVariant &,QJsonObject &,const std::function<QJsonObject(const Card *)> &);
 friend void projectAINewgenerals(AIRequest &,Room &,ServerPlayer *,const std::function<QJsonObject(const Card *)> &);
 friend void projectAINewgeneralsPlanningFacts(AIWorldView &,Room &,const ServerPlayer *);
 friend void projectAIMountainrumourPlanningContext(AIRequest &,Room &,const ServerPlayer *,const QVariant &,QJsonObject &,const std::function<QJsonObject(const Card *)> &);
 friend bool aiNiwoResponseQuestion(const AIRequest &,Room &,const ServerPlayer *);
 friend void projectAIGuoChoiceContext(AIRequest &,Room &,ServerPlayer *);
 friend void projectAIJijiao(AIRequest &,Room &,ServerPlayer *);
 friend void projectAIShicaiPreviewWorld(AIWorldView &,Room &,ServerPlayer *);
 friend bool aiNiwoShelieChoicePlanningQuestion(const AIRequest &,Room &,const ServerPlayer *);
 friend void projectAIFcDIYPlanningContext(AIRequest &,Room &,const ServerPlayer *,const QVariant &,QJsonObject &,const std::function<QJsonObject(const Card *)> &,const std::function<QJsonObject(const Card *,int,const QString &)> &);
 AIWorldSkillQueriesScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIMaxCardsScope::WorldSkillQueries){}
 AIMaxCardsScope scope;
};
// Only the existing issued Zunwei seed evaluator creates this lexical root.
// Complete CardRules/correction/factory proof precedes clone and card queries.
class AIZunweiSeedScope final {
 friend class AiDecisionCoordinator;
 AIZunweiSeedScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIMaxCardsScope::ZunweiSeed){}
 AIMaxCardsScope scope;
};
// Original SP Jilei literal ExNihilo clone and isCardLimited(hand=true).
// Separate full CardRules dependency/custody root; no Skill query grants it.
class AIOwnerJileiScope final {
public:
 AIOwnerJileiScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIMaxCardsScope::OwnerJilei){}
 bool accepted()const noexcept{return scope.accepted();}
private:
 AIMaxCardsScope scope;
};
// Original four owner canPindian projections; full public Pindian prohibit
// type family precedes any target/private-hand branch. No Card policy grant.
class AIOwnerPindianScope final {
public:
 AIOwnerPindianScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIMaxCardsScope::OwnerPindian){}
 bool accepted()const noexcept{return scope.accepted();}
private:
 AIMaxCardsScope scope;
};
// Original Yjcm2015 public inMyAttackRange matrix only. The full public
// metadata and correction family precedes all applicability. No Card policy.
class AIPublicAttackRangeMatrixScope final {
 friend void projectAIYjcm2015(AIWorldView &,Room &,const ServerPlayer *,const std::function<QJsonObject(const Card *)> &);
 AIPublicAttackRangeMatrixScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIMaxCardsScope::PublicAttackRangeMatrix){}
 AIMaxCardsScope scope;
};
// Original owner hasDefensiveHorse and view-as-equipment declaration loop.
// Complete metadata/correction/provider/recipe family before applicability;
// each call site has an independent lifetime and no Card policy custody.
class AIOwnerEquipmentQueriesScope final {
 friend class AiDecisionCoordinator;
 friend void projectAIStrategyOwner(AIWorldView &,Room &,ServerPlayer *);
 AIOwnerEquipmentQueriesScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIMaxCardsScope::OwnerEquipmentQueries){}
 AIMaxCardsScope scope;
};
// A distinct root lifetime: never prolongs the preceding MaxCards scope.
class AIAttackRangeScope final {
public:
 AIAttackRangeScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIMaxCardsScope::AttackRangeFixedExtra){}
 bool accepted()const noexcept{return scope.accepted();}
private:
 AIMaxCardsScope scope;
};
class AIDistanceScope final {
public:
 AIDistanceScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIMaxCardsScope::PublicDistances){}
 bool accepted()const noexcept{return scope.accepted();}
private:
 AIMaxCardsScope scope;
};
class AIOwnerCardRulesScope final {
public:
 AIOwnerCardRulesScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIMaxCardsScope::OwnerCardRules){}
 bool accepted()const noexcept{return scope.accepted();}
private:
 AIMaxCardsScope scope;
};
class AIOwnerSlashScope final {
public:
 AIOwnerSlashScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIMaxCardsScope::OwnerSlashWithoutCrossbow){}
 bool accepted()const noexcept{return scope.accepted();}
private:
 AIMaxCardsScope scope;
};
// Only Player's original no-argument Slash allocation can issue this receipt.
class AIPlayerSlashBirth final {
 friend class Player;
 AIPlayerSlashBirth(Room &,const Player *,const Card *) noexcept;
};
class AIOwnerAnalepticScope final {
public:
 AIOwnerAnalepticScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIMaxCardsScope::OwnerAnaleptic){}
 bool accepted()const noexcept{return scope.accepted();}
private:
 AIMaxCardsScope scope;
};
class AIOwnerAnalepticBirth final {
 friend void projectAIStrategyOwner(AIWorldView &,Room &,ServerPlayer *);
 AIOwnerAnalepticBirth(Room &,const ServerPlayer *,const Analeptic *) noexcept;
};
class AIFixedSlashProbesScope final {
public:
 AIFixedSlashProbesScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIMaxCardsScope::FixedSlashProbes){}
 bool accepted()const noexcept{return scope.accepted();}
private:
 AIMaxCardsScope scope;
};
class AIOwnerEvaluationsScope final {
public:
 AIOwnerEvaluationsScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIMaxCardsScope::FixedOwnerEvaluations){}
 bool accepted()const noexcept{return scope.accepted();}
 // Validates the input BEFORE the factory runs; never certifies its result.
 bool prepareFactory(const QString &name)noexcept{return scope.prepareLiteralFactory(name);}
private:
 AIMaxCardsScope scope;
};
// Only the two original hand/equip candidate loops use this root. No factory
// product receives a policy receipt here; custody comes from the physical map.
class AIOwnerCandidatesScope final {
public:
 AIOwnerCandidatesScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIMaxCardsScope::OwnerPhysicalCandidates){}
 bool accepted()const noexcept{return scope.accepted();}
private:
 AIMaxCardsScope scope;
};
// Created only by the actual source-cache bridge, before its requested-player
// selection or cache access. This root grants no Card policy or factory custody.
class AIConnectionQueryScope final {
 friend QJsonObject queryAIConnectionCache(Room&,const AIRequest&,ServerPlayer*,const QJsonObject&);
 AIConnectionQueryScope(Room&r,ServerPlayer*p,lua_State*L)
  :scope(r,p,L,AIMaxCardsScope::ConnectionQuery){}
public:
 bool accepted()const noexcept{return scope.accepted();}
private:
 AIMaxCardsScope scope;
};
bool inAIConnectionQueryScope(const Room &,lua_State *) noexcept;
class AIAnalepticNullBirth final {
 friend class Analeptic;
 AIAnalepticNullBirth(Room &,const Player *,const Card *) noexcept;
};
// Only the original Engine named factory can issue this product receipt. No
// public function accepts a later pointer/name/value to mint clone authority.
class AICardRulesCloneBirth final {
 friend class Engine;
 AICardRulesCloneBirth(const Engine *,const Card *,const QString &) noexcept;
};
// Only the actual native Engine cloneSkillCard return site can issue custody.
class AIZunweiSkillCloneBirth final {
 friend class Engine;
 AIZunweiSkillCloneBirth(const Engine *,const Card *,const QString &) noexcept;
};

#endif

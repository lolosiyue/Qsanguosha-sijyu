#ifndef AI_STRATEGY_OFFICIAL_H
#define AI_STRATEGY_OFFICIAL_H

#include "ai.h"
#include "ai-strategy/owner.h"
#include "structs.h"
#include <QJsonObject>
#include <functional>
#include <QString>
#include <memory>
#include <QList>
#include <QStringList>
#include <QJsonArray>

struct AICardConversionView;
struct AICardSpec;
struct AIEventView;
struct AINaturalFilterProof;
struct AIRequest;
struct AIWorldView;
class Card;
struct CardsMoveStruct;
struct CardUseStruct;
struct lua_State;
class Room;
class ServerPlayer;
class Skill;
struct SkillContext;

void projectAIStrategyYCZH2016(AIRequest &, Room &, ServerPlayer *viewer);

// Stage only after final acceptance. These hints are invisible until the same
// accepted decision receives its exact public skill-use/player-choice witness.
bool stageAIStrategyPlayerHints(Room &, const AIRequest &, const QJsonArray &, QString *error = nullptr, bool commit = true);
void witnessAIStrategyPlayerHints(Room &, quint64 decisionId, const ServerPlayer *source,
                                  const QString &skill, const QStringList &targets);
// The native card identity stays Room-local. Only this exact accepted product's
// later public CardUsed witness can publish its hints. Unrelated same-skill
// replacements and destroyed/rejected products cannot borrow a pending decision.
// Native admission may explicitly transfer a verified rebuild below.
bool stageAcceptedAIStrategyCardHints(Room &, const AIRequest &, const QJsonArray &,
                                     const Card *acceptedCard, QString *error = nullptr, bool commit = true);
// Only native successful V2 rebuilding may transfer an already accepted exact
// pointer. Both complete provenance refs and the immutable product must match.
void transferAcceptedAIStrategyCardHints(Room &, const Card *acceptedCard,
    const Card *rebuiltCard, const ServerPlayer *source,
    const SkillInstanceRef &activationRef, const SkillInstanceRef &sourceRef);
void witnessActualAIStrategyCardHints(Room &, const Card *actualCard,
                                      const ServerPlayer *source, const QStringList &targets);
// Accepted Jiaozhao PlayerChosen is tied to the live native skill execution and
// original use-card identity. ChoiceMade itself is not a public disclosure.
bool stageAcceptedAIStrategyChoiceHints(Room &, const AIRequest &, const QJsonArray &,
                                       const ServerPlayer *choice, QString *error = nullptr, bool commit = true);
// Call after target interception/cancellation, immediately before the native
// Jiaozhao declaration effect opens AG. No opaque execution ID is exported.
void witnessAIStrategyJiaozhaoDeclaration(Room &, const SkillContext &, const ServerPlayer *target);
// Call only at the original reader point, with this active request's viewer.
// Does not read the donor's unrestricted Player tags or change Room revision.
QJsonObject consumeAIStrategyPlayerHint(Room &, const AIRequest &, ServerPlayer *, const QString &name);

void projectAIStrategyYCZH2017(AIRequest &, Room &, ServerPlayer *,
    const std::function<QJsonObject(const Card *)> &publicDecisionCard);

// Owner tags and narrow public legality facts. No legacy strategy execution.
void projectAIStrategyYjcm2012(AIWorldView &, Room &, ServerPlayer *);

// Owner-only named strategy reads, plus the public presence of a player AI.
// The descriptor callback must expose only the viewer's active use card.
void projectAIYjcm2014(AIWorldView &, Room &, const ServerPlayer *,
                      const std::function<QJsonObject(const Card *)> &);

// Owner tags and already-disclosed borrowed card identity only. The callback
// serializes the owner's public declared use, never an undisclosed foreign hand.
void projectAIYjcm2015(AIWorldView &, Room &, const ServerPlayer *,
                      const std::function<QJsonObject(const Card *)> &publicDecisionCard);

// Only the named active inquiries receive these public participant names.
void projectAINewstarContext(AIRequest &, Room &, const ServerPlayer *);
// Readonly owner preview: no subcards, skill name, candidate or action receipt.
QJsonObject evaluateAINengchenPreview(Room &, ServerPlayer *, const AIRequest &, int materialId);

QJsonObject queryAICollateralFlag(Room&,const AIRequest&,ServerPlayer*,const QJsonObject&);
class AICollateralEffect final {
public:
 AICollateralEffect(Room&,ServerPlayer*killer,ServerPlayer*victim,ServerPlayer*user,const Card*,const QString&prompt);
 ~AICollateralEffect();
 AICollateralEffect(const AICollateralEffect&)=delete;
 AICollateralEffect&operator=(const AICollateralEffect&)=delete;
private:struct Data;std::unique_ptr<Data>d;friend struct AICollateralSlashAccess;friend class AICollateralSlashQuestion;
};
class AICollateralSlashQuestion final {
public:
 AICollateralSlashQuestion(Room&,ServerPlayer*,const QString&,const QString&,int);
 ~AICollateralSlashQuestion();
 AICollateralSlashQuestion(const AICollateralSlashQuestion&)=delete;
 AICollateralSlashQuestion&operator=(const AICollateralSlashQuestion&)=delete;
private:struct Data;std::unique_ptr<Data>d;friend struct AICollateralSlashAccess;
};
void projectAICollateralSlash(AIRequest&,Room&,ServerPlayer*);

// Native observation only. This constructor is inserted at the one compiled
// TenyearBenxi producer; it does not set a property, choose, or pay anything.
class AIBenxiCollateralSource final {
public:
 AIBenxiCollateralSource(Room&,ServerPlayer*,const CardUseStruct&);
 ~AIBenxiCollateralSource();
 AIBenxiCollateralSource(const AIBenxiCollateralSource&)=delete;
 AIBenxiCollateralSource&operator=(const AIBenxiCollateralSource&)=delete;
private: struct Data;std::unique_ptr<Data>d;friend struct AIBenxiCollateralAccess;friend class AIBenxiCollateralQuestion;
};
// Every actual use-card inquiry masks an outer inquiry, including same-pattern
// nested asks. A source scope can bind only its first exact inquiry.
class AIBenxiCollateralQuestion final {
public:
 AIBenxiCollateralQuestion(Room&,ServerPlayer*,const QString&,const QString&,int);
 ~AIBenxiCollateralQuestion();
 AIBenxiCollateralQuestion(const AIBenxiCollateralQuestion&)=delete;
 AIBenxiCollateralQuestion&operator=(const AIBenxiCollateralQuestion&)=delete;
private:struct Data;std::unique_ptr<Data>d;friend struct AIBenxiCollateralAccess;
};
void projectAIBenxiCollateral(AIRequest&,Room&,ServerPlayer*);
bool aiBenxiCollateralCurrent(const AIRequest&,Room&,ServerPlayer*,const QString&wire);
// Parse at the reached source call, never during projection. A non-null virtual
// product transfers to the existing readonly store; a physical product is borrowed.
const Card *parseAIBenxiCollateral(const AIRequest&,Room&,ServerPlayer*,const QString&wire,bool *authorized);

void registerAIRangyiSource(Room&,lua_State*);
// Native accepted Room use only, immediately around the existing onUse call.
class AIRangyiUse final {
public:AIRangyiUse(Room&,const CardUseStruct&);~AIRangyiUse();
private:struct Data;std::unique_ptr<Data>d;friend struct AIRangyiAccess;friend class AIRangyiCallback;
};
// Inside the real LuaSkillCard::use bridge, after on_use.push and before pcall.
class AIRangyiCallback final {
public:AIRangyiCallback(Room&,lua_State*,int,const Card*,ServerPlayer*,const QList<ServerPlayer*>&);~AIRangyiCallback();
private:struct Data;std::unique_ptr<Data>d;friend struct AIRangyiAccess;friend class AIRangyiQuestion;
};
// Every UseCard question masks its parent. A nested question permanently
// revokes an already-bound Rangyi question; no restoration of old authority.
class AIRangyiQuestion final {
public:AIRangyiQuestion(Room&,ServerPlayer*,const QString&,const QString&,int);~AIRangyiQuestion();
private:struct Data;std::unique_ptr<Data>d;friend struct AIRangyiAccess;
};
void projectAIRangyi(AIRequest&,Room&,ServerPlayer*);
bool aiRangyiCurrent(const AIRequest&,Room&,ServerPlayer*,int);
// Exact raw LuaSkillCard product, never a forged V2 activation instance.
// Caller owns virtual return. selected adds native physical availability gate.
Card*createAIRangyiProduct(const AIRequest&,Room&,ServerPlayer*,int,bool selected,QString* =nullptr);
Card*rebuildAIRangyiProduct(const AIRequest&,Room&,ServerPlayer*,const AICardConversionView&);

// The capture selector is authenticated by the coordinator's active readonly
// store (or exact disclosed native identity). No wire, face or ticket is trusted.
QString captureAIFaqiPlan(const AIRequest &,ServerPlayer *,int offeredId,
    AIReadonlyCardLease, const QStringList &targets,QString *error=nullptr,
    const QJsonObject &sourceFields=QJsonObject());
enum class AIFaqiOutcome { Unsupported, NativeRejected, Ready, SourcePass, SourceError };
const Card *restoreAIFaqiPlan(const AIRequest &,ServerPlayer *,const QString &token,
    QStringList *targets,QString *error=nullptr,AIFaqiOutcome*sourceStatus=nullptr);
void clearAIFaqiPlans(Room &);
void interruptAIFaqiPlansAsync(Room &) noexcept;
// No issuer/payment lives here. This reconstructs ONLY the exact saved source
// string in the current native question and calls a guarded native admission
// supplied by the coordinator. A failed native rule is distinct from missing
// observation/provenance. Never dispatches a legacy AI callback.
struct AIFaqiSelection {
    AIFaqiOutcome outcome=AIFaqiOutcome::Unsupported;
    QString error,sourceWire;
    QStringList targets;
    CardUseStruct use;
};
AIFaqiSelection prepareAIFaqiSelection(const AIRequest &,ServerPlayer *,
    const QString &,const std::function<bool(CardUseStruct &)> &guardedNativeAdmission);
// Finite readonly source constructors; nil is an actual missing prototype.
std::unique_ptr<Card> constructAIFaqiRevise(const AIRequest &,ServerPlayer *,
 const QString &kind,const QList<int> &,QString *error);
// Selected receipts reserve a normal ledger ID but are validated exclusively by
// this finite helper. They do not impersonate a conversion skill instance.
QJsonObject issueAIFaqiReceipt(const AIRequest &,ServerPlayer *,const QString &token,
 const std::function<bool(CardUseStruct &)> &admit);
bool hasAIFaqiReceipt(const AIRequest &,int id);
bool applyAIFaqiReceipt(const AIRequest &,ServerPlayer *,const AICardSpec &,
 const QStringList &targets,const std::function<bool(CardUseStruct &)> &admit,CardUseStruct &);

// Native-only SWIG adapters. Owned QList is tracked and idempotently finalized;
// Card remains the exact retained producer pointer, never a clone or ticket.
bool pushAISlotCard(lua_State*,const Card*);
bool matchAISlotCard(lua_State*,int,const Card*);
bool pushAISlotTargets(lua_State*,const QList<ServerPlayer*>&);
bool readAISlotTargets(lua_State*,int,QList<ServerPlayer*>&);
QString writeAIFaqiSlot(const AIRequest&,ServerPlayer*,int,
 AIReadonlyCardLease,const QStringList&,const QJsonObject&,QString*error=nullptr);
enum class AIFaqiSlotStatus { Unknown, Card, SourcePass, SourceError };
const Card*readAIFaqiSlot(const AIRequest&,ServerPlayer*,const QString&,
 QStringList*,int*offered,QString*error=nullptr,AIFaqiSlotStatus*status=nullptr);
bool hasAIFaqiSlot(Room&,ServerPlayer*,const QString&);
// Runtime reset/shutdown only, before releasing game Lua state. Never a generic
// gameplay-entry clear: this is custody of the actual synchronized source slot.
void clearAIFaqiSlots(Room&);

// Native-only effect proof services. No service may execute the supplied Lua
// closure or use a private branch outcome to decide whether to prove it.
struct AIPoxiEffectProof {
 std::function<bool(lua_State*,int,const QString&,QString*)> luaFunction;
 std::function<bool(const Skill*,const QString&,QString*)> nativeSkill;
 std::function<bool(const QString&,QString*)> dependency;
};
// Exhaustive runtime definition union, independent of ownership, validity,
// holder, correction result, or the actual spjili branch outcome.
bool proveAIPoxiCorrectionUnion(Room&,lua_State*,const AIPoxiEffectProof&,QString*reason=nullptr);
// Proves the reached shouldSuppressIntention callee, preserving every branch.
// methodIndex must be resolved by the shared exact SmartAI instance/class seal.
// namespace index must be the shared captured sgs namespace, not a lookalike.
bool proveAIPoxiSuppressionBindings(Room&,lua_State*,int methodIndex,int namespaceIndex,
 const AIPoxiEffectProof&,QString*reason=nullptr);
// Exact event-specific body obligations. Shared caller must still prove the
// full filterEvent prelude, registries, and every earlier native event root.
// The five-argument entry has no implicit positive effect service.
bool certifyAIPoxiFilterBody(Room&,lua_State*,int selfIndex,const AINaturalFilterProof&,QString*reason=nullptr);
bool certifyAIPoxiFilterBody(Room&,lua_State*,int selfIndex,const AINaturalFilterProof&,
 const AIPoxiEffectProof&,QString*reason=nullptr);

// Called only after the actual recipient's notification has been sent.
void recordAIPoxiNotification(Room &,ServerPlayer *,const QList<int>&,
    const QString &reason,bool active);
void projectAILeiExchange(AIRequest &,Room &,ServerPlayer *);
int aiLeiPrototype(const AIRequest &,const ServerPlayer *,const QString &,const QString &);
bool authorizeAIPoxiEvaluation(const AIRequest &,int cardId,QJsonObject *face=nullptr);
bool authorizeAILeiPayment(const AIRequest &,Room &,ServerPlayer *,
    const SkillInstanceRef &activation,const SkillInstanceRef &source,const QList<int>&,
    QList<int> *foreignIds=nullptr);

// Finite own game-VM LuaAI slot only. No observation ledger, gameplay property,
// card authority, private hand lookup, or arbitrary slot name.
QJsonObject queryAIPoxiSlot(Room &,const AIRequest &,ServerPlayer *,
                                 const QString &operation,const QString &publicTarget);

int aiLimuPrototype(const AIRequest &,const ServerPlayer *,const QString &,const QString &);
std::unique_ptr<Card> aiLimuEvaluation(const AIRequest &,Room &,ServerPlayer *,const QString &,const QList<int> &,QString *);

QJsonObject aiChuliEvent(const AIEventView&);
QJsonObject queryAIChuliMap(Room&,const AIRequest&,ServerPlayer*,const QString&,const QJsonObject&);

QJsonObject queryAIDaojiCard(Room&,const AIRequest&,ServerPlayer*,const QString&,const QJsonObject&);

QJsonObject queryAIManjuanPrinted(Room&,const AIRequest&,ServerPlayer*,const QString&,const QJsonObject&);

const Card*constructAIManjuanRaw(const AIRequest&,ServerPlayer*,const QString&,int*selected);
bool hasAIManjuanRaw(const AIRequest&,int conversionId);
bool applyAIManjuanRaw(const AIRequest&,ServerPlayer*,const AICardSpec&,const QStringList&,
 const std::function<bool(CardUseStruct&)>&,CardUseStruct&);

// Finite current-source operations. Reference tokens are equality evidence only,
// never cards, selected receipts, or permission to inspect hidden live faces.
QJsonObject queryAIManjuan(Room&,const AIRequest&,ServerPlayer*,const QString&,const QJsonObject&);

int aiGuanduTwyjPrototype(const AIRequest &, const ServerPlayer *, const QString &, const QString &);
// Call only after the real recipient-redacted notification was sent.
void recordAIShicaiPreview(Room &, ServerPlayer *, const QList<CardsMoveStruct> &, bool isLostPhase);
void invalidateAIShicaiPreview(Room &, int cardId);
void clearAIShicaiPreview(Room &);
void projectAIShicaiPreviewWorld(AIWorldView &, Room &, ServerPlayer *);
// No caller-supplied card ID; returns only the same active, witnessed top card.
// It grants readonly evaluation, never permission to select/pay the preview.
const Card *aiShicaiPreview(const AIRequest &, Room &, ServerPlayer *);

// Finite fixed-source prototypes only; these functions grant no action scope.
int aiAssassinsHappy2v2ReadonlyPrototype(const QString &skill, const QString &prototype);
bool aiFenyueEvaluation(const AIRequest &, const QString &skill,
                              const QString &name, const QJsonArray &costs);

#endif

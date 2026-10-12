#ifndef AI_STRATEGY_OL_AI_H
#define AI_STRATEGY_OL_AI_H

#include "ai-strategy/owner.h"
#include "ai.h"
#include "structs.h"
#include <QJsonObject>
#include <QString>
#include <QVariant>
#include <functional>
#include <memory>
#include <QStringList>
#include <QList>

struct AICardSpec;
struct AIEventView;
struct AIRequest;
struct AiSkillActionContext;
class Card;
struct CardUseStruct;
struct lua_State;
struct lua_TableObservation;
class QString;
class QVariant;
class Room;
class ServerPlayer;
struct SkillContext;

// Finite source hypotheses only; never payment or an activation-scope issuer.
bool aiOlClanActionConstruction(const AIRequest &, const ServerPlayer *,
                                const QJsonObject &, QString *error = nullptr);
// Call only on the newly constructed detached hypothesis, before rule queries
// and readonly recipe issuance. This cannot mutate a physical room card.
bool applyAIOlClanActionConstruction(const AIRequest &, const ServerPlayer *,
                                     const QJsonObject &, Card &);
// -1 absent; bit0 source material permission. Existing live skill scope and
// native canSelect/feasible/create remain mandatory when selected.
int aiOlClanActionPrototype(const QString &skill, const QString &prototype);

// Exact fixed olClan questions only; an owner tag never grants a hidden face.
void projectAIStrategyOlClanObservations(AIRequest &, Room &, ServerPlayer *,
    const QVariant &, QJsonObject &context,
    const std::function<QJsonObject(const Card *)> &publicCard);
// Source prototype metadata only; issued scope and final native payment remain required.
int aiOlClanObservationPrototype(const AIRequest &, const ServerPlayer *,
    const QString &skill, const QString &prototype);

// Readonly only. Complete source tuple/current question/material disclosure
// validation; this never authorizes selection, a receipt, or a final payment.
bool aiOlClanConstruction(const AIRequest &, const ServerPlayer *, const QJsonObject &, QString *error=nullptr);
// Call only after the full construction predicate returned true. These two
// exceptions belong solely to the fixed source mutation/explicit shown-card.
bool aiOlClanRepeatedMaterials(const QJsonObject &);
bool aiOlClanForeignMaterial(const QJsonObject &);
int aiOlClanExtraTarget(const QJsonObject &);
int aiOlClanChengqiPrototype(const AIRequest &, const ServerPlayer *, const QString &, const QString &);
void projectAIOlClanPlanContext(AIRequest &, Room &, const ServerPlayer *);
// Native-only cross-question observation. Custody must come from the active
// runtime readonly store, never from a Lua-provided pointer or reconstructed wire.
QString captureAIOlClanObservation(const AIRequest &, ServerPlayer *,
    const QString &slot, AIReadonlyCardLease, QString *error=nullptr);
const Card *restoreAIOlClanObservation(const AIRequest &, ServerPlayer *,
    const QString &slot, const QString &token, QString *error=nullptr);
void clearAIOlClanObservations(Room &);

QJsonObject queryAIOlClanPlanSlot(const AIRequest&,ServerPlayer*,const QString&slot,const QString&operation,const QJsonObject&payload,AIReadonlyCardLease lease={});
void clearAIOlClanPlanSlots(Room&);
bool registerAIOlClanPlanSlotSources(Room&,lua_State*,QString*error=nullptr);

// Audited source: olClan.lua3260–3327, AI olClan-ai.lua1882–1888.
// This predicate grants only hypothetical Play planning, never a card action.
bool aiOlClanPlanningQuestion(const AIRequest &, Room &, const ServerPlayer *);
QJsonObject aiOlClanPlanningContext(const AIRequest &, Room &, const ServerPlayer *);

// sourceKey MUST come from the runtime-owned readonly store for this exact
// card/request, never a Lua field. This grants no action/payment capability.
bool aiQiuxinSource(const AIRequest &, const ServerPlayer *, const Card *,
                          const QString &sourceKey, QString *error = nullptr);
// Exact source targetFilter query on the retained original product. The caller
// holds normal game Lua/engine bindings; an outer owner-range scope is retained.
// Returns an error object for missing capability/input; never a guessed false.
QJsonObject aiQiuxinTargetFilter(Room &, ServerPlayer *, const AIRequest &,
                                     const Card *, const QString &sourceKey,
                                     const QJsonObject &query);

void projectAIOlClanZujiewuSuit(const AIRequest&,Room&,const ServerPlayer*,const QVariant&,QJsonObject&);
QJsonObject queryAIOlClanScalarSlot(const AIRequest&,ServerPlayer*,const QString&,const QJsonObject&);

// Two fixed source Player-valued scratch fields on the current own LuaAI.
QJsonObject queryAIOlClanPlayerSlot(const AIRequest&,ServerPlayer*,const QString&,const QString&,const QJsonObject&);

// Read-only evaluation authorization, never payment/submit authority. The caller
// must separately bind this immutable request to its active viewer and revision.
// No live cards, private tags, piles or Lua callbacks are read here.
bool authorizeAIOlClanNotifiedEvaluation(const AIRequest &request, int cardId,
    QJsonObject *disclosedCard = nullptr, QString *error = nullptr);
// Exact two zero-selection marked physical producers. Validates immutable
// viewer/mark/current disclosed face and presence of the expected native skill
// scope. The caller still validates the selected activation/source refs, quotas,
// current Room revision, createCard result, limits and target legality. This
// function reads no live card, Room tag or historical AG guess.
bool authorizeAIOlClanMarkedPhysicalProduct(const AIRequest &request, int productId,
    QJsonObject *disclosedCard = nullptr, bool *printedProduct = nullptr,
    QString *error = nullptr);

// Readonly fixed source constructor admission, never payment authorization.
bool aiOlClanJiePackagePlanningConstruction(const AIRequest &, const ServerPlayer *,
    const QJsonObject &spec, QString *error = nullptr);
// -1 absent; bit0 paid materials, bit1 declared userString. Native live source
// selection/factory validation is still mandatory at receipt/final rebuild.
int aiOlClanJiePackagePlanningPrototype(const AIRequest &, const ServerPlayer *,
    const QString &skill, const QString &prototype);
void projectAIJiePackagePlanningContext(AIRequest &, Room &, const ServerPlayer *,
    const std::function<QJsonObject(const Card *)> &publicCard);

bool aiFuzhaiQuxiConstruction(const AIRequest &,const ServerPlayer *,const QJsonObject &);

bool aiMiuyanZonglveConstruction(const AIRequest &,const ServerPlayer *,const QJsonObject &);

bool aiXixiangEvaluation(const AIRequest &,const ServerPlayer *,const QJsonObject &);

bool aiPanqinEvaluation(const AIRequest &,const ServerPlayer *,const QJsonObject &);

// Fixed ol_sp Choulie source constructor only. Neither function issues payment.
bool aiChoulieEvaluation(const AIRequest &, const ServerPlayer *, const QJsonObject &);
bool configureAIChoulieEvaluation(Card &);

// Native callsite-only scope around the two fixed OL/SP response inquiries.
class AIOlspQuestionScope final {
public:
 AIOlspQuestionScope(Room&, const SkillContext&, const QString &skill);
 ~AIOlspQuestionScope();
 AIOlspQuestionScope(const AIOlspQuestionScope&)=delete;
 AIOlspQuestionScope& operator=(const AIOlspQuestionScope&)=delete;
private: struct Data; std::unique_ptr<Data> d;
};
// Pre-request instance admission and resolved-root equality are separate because
// makeRequest projects scope rows before attaching its selected action context.
bool aiOlspInstance(Room&,const ServerPlayer*,const QString&,int instanceID);
bool aiOlspAction(Room&,const ServerPlayer*,const AiSkillActionContext&);
bool aiOlspQuestion(const AIRequest&,const ServerPlayer*);
bool aiOlspEvaluation(const AIRequest&,const ServerPlayer*,const QJsonObject&);

// Native-only SWIG adapters. Owned QList is tracked and idempotently finalized;
// Card remains the exact retained producer pointer, never a clone or ticket.
bool pushAISlotCard(lua_State*,const Card*);
bool matchAISlotCard(lua_State*,int,const Card*);
bool pushAISlotTargets(lua_State*,const QList<ServerPlayer*>&);
bool readAISlotTargets(lua_State*,int,QList<ServerPlayer*>&);
QString writeAIHeqiaSlot(const AIRequest&,ServerPlayer*,int,
 AIReadonlyCardLease,const QStringList&,const QJsonObject&,QString*error=nullptr);
enum class AIHeqiaSlotStatus { Unknown, Card, SourcePass, SourceError };
const Card*readAIHeqiaSlot(const AIRequest&,ServerPlayer*,const QString&,
 QStringList*,int*offered,QString*error=nullptr,AIHeqiaSlotStatus*status=nullptr);
bool hasAIHeqiaSlot(Room&,ServerPlayer*,const QString&);
// Runtime reset/shutdown only, before releasing game Lua state. Never a generic
// gameplay-entry clear: this is custody of the actual synchronized source slot.
void clearAIHeqiaSlots(Room&);
bool registerAIHeqiaSources(Room&,lua_State*,QString*error=nullptr);
std::unique_ptr<Card> constructAIHeqia(const AIRequest&,ServerPlayer*,int,int,QString*error=nullptr);
QJsonObject issueAIHeqiaReceipt(const AIRequest&,ServerPlayer*,const QString&,const QStringList&,const std::function<bool(CardUseStruct&)>&);
QJsonObject issueAIHeqiaFirst(const AIRequest&,ServerPlayer*,const QList<int>&,const QStringList&,const std::function<bool(CardUseStruct&)>&);
bool hasAIHeqiaReceipt(const AIRequest&,int);
bool applyAIHeqiaReceipt(const AIRequest&,ServerPlayer*,const AICardSpec&,const QStringList&,const std::function<bool(CardUseStruct&)>&,CardUseStruct&);

// Native-only SWIG adapters. Owned QList is tracked and idempotently finalized;
// Card remains the exact retained producer pointer, never a clone or ticket.
bool pushAISlotCard(lua_State*,const Card*);
bool matchAISlotCard(lua_State*,int,const Card*);
bool pushAISlotTargets(lua_State*,const QList<ServerPlayer*>&);
bool readAISlotTargets(lua_State*,int,QList<ServerPlayer*>&);
QString writeAILeiluanSlot(const AIRequest&,ServerPlayer*,const QString&,
 AIReadonlyCardLease,const QStringList&,const QJsonObject&,QString*error=nullptr);
enum class AILeiluanSlotStatus { Unknown, Card, SourcePass, SourceError };
const Card*readAILeiluanSlot(const AIRequest&,ServerPlayer*,const QString&,
 QStringList*,QString*offered,QString*error=nullptr,AILeiluanSlotStatus*status=nullptr);
bool hasAILeiluanSlot(Room&,ServerPlayer*,const QString&);
// Runtime reset/shutdown only, before releasing game Lua state. Never a generic
// gameplay-entry clear: this is custody of the actual synchronized source slot.
void clearAILeiluanSlots(Room&);
bool registerAILeiluanSources(Room&,lua_State*,QString*error=nullptr);
std::unique_ptr<Card> constructAILeiluan(const AIRequest&,ServerPlayer*,const QString&,QString*error=nullptr);
QJsonObject issueAILeiluanReceipt(const AIRequest&,ServerPlayer*,const QString&,const std::function<bool(CardUseStruct&)>&);
bool hasAILeiluanReceipt(const AIRequest&,int);
bool applyAILeiluanReceipt(const AIRequest&,ServerPlayer*,const AICardSpec&,const QStringList&,const std::function<bool(CardUseStruct&)>&,CardUseStruct&);

// Native-only SWIG adapters. Owned QList is tracked and idempotently finalized;
// Card remains the exact retained producer pointer, never a clone or ticket.
bool pushAISlotCard(lua_State*,const Card*);
bool matchAISlotCard(lua_State*,int,const Card*);
bool pushAISlotTargets(lua_State*,const QList<ServerPlayer*>&);
bool readAISlotTargets(lua_State*,int,QList<ServerPlayer*>&);
QString writeAIJianmanSlot(const AIRequest&,ServerPlayer*,const QString&,
 AIReadonlyCardLease,const QStringList&,const QJsonObject&,QString*error=nullptr);
enum class AIJianmanSlotStatus { Unknown, Card, SourcePass, SourceError };
const Card*readAIJianmanSlot(const AIRequest&,ServerPlayer*,const QString&,
 QStringList*,QString*offered,QString*error=nullptr,AIJianmanSlotStatus*status=nullptr);
bool hasAIJianmanSlot(Room&,ServerPlayer*,const QString&);
// Runtime reset/shutdown only, before releasing game Lua state. Never a generic
// gameplay-entry clear: this is custody of the actual synchronized source slot.
void clearAIJianmanSlots(Room&);
bool registerAIJianmanSources(Room&,lua_State*,QString*error=nullptr);
std::unique_ptr<Card> constructAIJianman(const AIRequest&,ServerPlayer*,const QString&,QString*error=nullptr);
QJsonObject issueAIJianmanReceipt(const AIRequest&,ServerPlayer*,const QString&,const std::function<bool(CardUseStruct&)>&);
bool hasAIJianmanReceipt(const AIRequest&,int);
bool applyAIJianmanReceipt(const AIRequest&,ServerPlayer*,const AICardSpec&,const QStringList&,const std::function<bool(CardUseStruct&)>&,CardUseStruct&);

// Constructed ONLY at the exact compiled Xuanzhu2Card::onUse askForUseCard callsite.
class AIXuanzhuQuestionScope final {
public:
 AIXuanzhuQuestionScope(Room&,ServerPlayer*,const Card*,const QString&);
 ~AIXuanzhuQuestionScope();
 AIXuanzhuQuestionScope(const AIXuanzhuQuestionScope&)=delete;
 AIXuanzhuQuestionScope& operator=(const AIXuanzhuQuestionScope&)=delete;
private:struct State;std::unique_ptr<State>d;
};
// Installed at EVERY native askForUseCardStruct entry, including unrelated/nested calls.
class AIXuanzhuInquiryScope final {
public:
 AIXuanzhuInquiryScope(Room&,ServerPlayer*,const QString&,const QString&,int);
 ~AIXuanzhuInquiryScope();
 AIXuanzhuInquiryScope(const AIXuanzhuInquiryScope&)=delete;
 AIXuanzhuInquiryScope& operator=(const AIXuanzhuInquiryScope&)=delete;
private:struct State;std::unique_ptr<State>d;
};
// Called only after native makeRequest has completed its actual world projection.
void bindAIXuanzhuQuestion(const AIRequest&,ServerPlayer*);
bool aiXuanzhuQuestion(const AIRequest&,ServerPlayer*);
std::unique_ptr<Card> constructAIXuanzhuFirst(const AIRequest&,ServerPlayer*,QString*);
QJsonObject issueAIXuanzhuFirst(const AIRequest&,ServerPlayer*,AIReadonlyCardLease,
 const std::function<bool(CardUseStruct&)>&);
QJsonObject issueAIXuanzhuSaved(const AIRequest&,ServerPlayer*,int,const QString&,const QStringList&,
 const std::function<bool(CardUseStruct&)>&);
bool hasAIXuanzhuReceipt(const AIRequest&,int);
bool applyAIXuanzhuReceipt(const AIRequest&,ServerPlayer*,const AICardSpec&,const QStringList&,
 const std::function<bool(CardUseStruct&)>&,CardUseStruct&);

bool registerAIXuanzhuSlotSources(Room&,lua_State*,QString*error=nullptr);
void clearAIXuanzhuSlots(Room&);
QString writeAIXuanzhuSlot(const AIRequest&,ServerPlayer*,AIReadonlyCardLease,const QStringList&,const QJsonObject&,QString*error=nullptr);
struct AIXuanzhuSlotRead{
 enum Kind{Unknown,NilPlan,BooleanPlan,TargetsError,Targets,CardError,Card};Kind kind=Unknown;
 QString error,name;QStringList targets;AIReadonlyCardLease lease;bool targetsNil=false,targetsBoolean=false,cardNil=false;
};
AIXuanzhuSlotRead readAIXuanzhuSlot(const AIRequest&,ServerPlayer*,bool includeCard);
std::unique_ptr<Card> constructAIXuanzhuPolicy(const AIRequest&,ServerPlayer*,const QString&,QString*error=nullptr);

void projectAIOLGeneralContext(Room &,ServerPlayer *,const QStringList &,QJsonObject &);
void captureAIOLJijiang(Room &);
void beginAIOLJijiang(Room &,const QString &viewer);
void invalidateAIOLJijiang(Room &);
QJsonObject aiOLJijiangEvent(const AIEventView &);
QJsonObject queryAIOLJijiang(Room &,const AIRequest &,ServerPlayer *,const QString &op,const QString &value=QString());

// Native OL constructors only; the reader is used by declared PreCardUsed
// projection, with no Lua lookup or permission to disclose other skill names.
void recordAIFreshOLJijiangSkills(Room&,const Card*);
QJsonObject readAIOLJijiangConstructedSkills(Room&,ServerPlayer*,const Card*);
// Operation identity is issued only by the compiled OL pay/effect call sites.
// It conveys the actual response source, never a plan or payment authority.
class AIJijiangScope {
public:
 AIJijiangScope(Room&,const SkillContext&); // OLJijiangVS::pay
 AIJijiangScope(Room&,const SkillContext&,int event,bool effect);
 ~AIJijiangScope();
 AIJijiangScope(const AIJijiangScope&)=delete;
 AIJijiangScope&operator=(const AIJijiangScope&)=delete;
 struct Data;Data*d;
};
class AIJijiangLiege {
public:AIJijiangLiege(Room&,ServerPlayer*);~AIJijiangLiege();
 AIJijiangLiege(const AIJijiangLiege&)=delete;
 AIJijiangLiege&operator=(const AIJijiangLiege&)=delete;
 struct Data;Data*d;
};
class AIJijiangEvent {
public:AIJijiangEvent(Room&,int,ServerPlayer*,const QVariant&);~AIJijiangEvent();
 AIJijiangEvent(const AIJijiangEvent&)=delete;
 AIJijiangEvent&operator=(const AIJijiangEvent&)=delete;
 struct Data;Data*d;
};
void enterAIJijiangEvent(Room&,int,ServerPlayer*);
void enterAIJijiangFilter(Room&,int,ServerPlayer*,const QVariant&);
void clearAIJijiang(Room&);
bool initializeAIJijiang(Room&,lua_State*);
QJsonObject queryAIJijiang(Room&,const AIRequest&,ServerPlayer*,const QString&,const QString&);

// Compatibility with existing GameVM observers. OL no longer executes a
// GameVM thunk or exports shared Lua tables, so these issue no exemptions.
void observeAIJijiangEntry(Room&,lua_State*,int) noexcept;
bool acceptAIJijiangEntry(Room&,lua_State*,int) noexcept;
bool allowAIJijiangWrite(Room&,lua_State*,const lua_TableObservation&) noexcept;

// Exact compiled OLShebian cost/effect calls only; no policy constructor.
class AIShebianScope {
public:
 AIShebianScope(Room&,const SkillContext&,bool effect);
 ~AIShebianScope();
 AIShebianScope(const AIShebianScope&)=delete;
 AIShebianScope&operator=(const AIShebianScope&)=delete;
 struct Data;Data*d;
};
void invalidateAIShebian(Room&);
void enterAIShebianInquiry(Room&,ServerPlayer*,const QString&,const QString&);
void clearAIShebian(Room&);
bool initializeAIShebian(Room&,lua_State*);
QJsonObject queryAIShebian(Room&,ServerPlayer*,const AIRequest&,const QJsonObject&);

QJsonObject refreshAIOrdinaryCardChoice(Room&,ServerPlayer*,const AIRequest&);

void projectAIStrategyOlStandardResponse(AIRequest &request,Room &room,ServerPlayer *viewer,QJsonObject &context);
// Bit 0: paid prototype; bit 1: declared prototype. -1: not audited.
int aiOlStandardResponsePrototype(const AIRequest &,const ServerPlayer *,const QString &,const QString &);
bool aiOlStandardResponseConstruction(const AIRequest &,const ServerPlayer *,const QJsonObject &);

#endif

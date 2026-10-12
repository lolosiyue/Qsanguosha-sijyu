#ifndef AI_STRATEGY_XUFA_H
#define AI_STRATEGY_XUFA_H

#include "ai-strategy/owner.h"
#include <memory>
#include <QList>
#include <QString>
#include <QSet>
#include <QJsonObject>
#include <QStringList>
#include <functional>

struct AICardSpec;
struct AINaturalFilterProof;
struct AIRequest;
class Card;
struct CardsMoveStruct;
struct CardUseStruct;
struct lua_State;
class LuaCallbackRef;
class Player;
class QVariant;
class Room;
class ServerPlayer;

// Only instantiated at the compiled recordAiEvent -> actual filterEvent pair.
// The row is not readable while pending; destruction never restores a row.
class AIXufaEventScope {
public:
 AIXufaEventScope(Room&,int,ServerPlayer*,const QVariant&);
 ~AIXufaEventScope();
 AIXufaEventScope(const AIXufaEventScope&)=delete;
 AIXufaEventScope&operator=(const AIXufaEventScope&)=delete;
private:struct Data;std::unique_ptr<Data>d;friend struct AIXufaEventAccess;
};
bool pendingAIXufaEvent(Room&);
bool prepareAIXufaFilterCallback(Room&,const char*method);
void completeAIXufaEventProof(Room&,lua_State*,int);

// Install only around the two compiled XufaCard::use move statements. Native
// custody of owner-selected IDs, not a serialized reason or a Lua capability.
class AIXufaMoveBodyScope {
public:
 AIXufaMoveBodyScope(Room&,ServerPlayer*,const Card*,bool discard);
 ~AIXufaMoveBodyScope();
 AIXufaMoveBodyScope(const AIXufaMoveBodyScope&)=delete;
 AIXufaMoveBodyScope&operator=(const AIXufaMoveBodyScope&)=delete;
private:
 struct Data;std::unique_ptr<Data>d;
 friend struct AIXufaBodyAccess;
};
// Installed at EVERY moveCardsSub entry, before normalization or callbacks.
// One pending compiled Xufa call is consumed once; nested moves push a denied
// frame and cannot inherit the parent's custody.
class AIXufaMoveEntry {
public:
 AIXufaMoveEntry(Room&,const QList<CardsMoveStruct>&,bool visible,bool guanxing);
 ~AIXufaMoveEntry();
 AIXufaMoveEntry(const AIXufaMoveEntry&)=delete;
 AIXufaMoveEntry&operator=(const AIXufaMoveEntry&)=delete;
private:
 struct Data;std::unique_ptr<Data>d;
 friend struct AIXufaBodyAccess;
};
// Trusted bootstrap only; hashes are compiled constants, never file-derived.
bool registerAIXufaBodySources(Room&,lua_State*,QString*reason=nullptr);
// This certifies ONLY this filterEvent body/callback union. The caller must
// separately certify the common prefix, actual self, outer native graph,
// asynchronous hooks/finalizers, and saved-plan lifetime. No continuity lease.
bool certifyAIXufaFilterBody(Room&,lua_State*,int selfIndex,
 const AINaturalFilterProof&,QString*reason);
bool matchesAIXufaMoveEvent(Room&,int,ServerPlayer*,const QVariant&);

// Exact Xufa native-query lifetime. The source-only metadata check below is
// insufficient without this complete correction/allocation/notification scope.
// It never admits a TriggerSkill, move event, inquiry or event snapshot.
class AIXufaCorrectionScope final {
 friend class AIXufaQueryScope;
 AIXufaCorrectionScope(Room &,ServerPlayer *,lua_State *);
public:
 ~AIXufaCorrectionScope();
 bool accepted() const noexcept;
 AIXufaCorrectionScope(const AIXufaCorrectionScope &)=delete;
 AIXufaCorrectionScope &operator=(const AIXufaCorrectionScope &)=delete;
private:
 struct Data;std::unique_ptr<Data>d;
 friend bool proveAIXufaCorrectionUnion(Room &,lua_State *,QString *);
};
// Xufa-only nonwriter proof. This does not authorize a saved value, a move,
// an event, private knowledge, payment, or preservation of another ledger.
bool registerAIXufaGetterSources(Room&,lua_State*,QString*reason=nullptr);
bool proveAIXufaGetterFunction(Room&,lua_State*,int functionIndex,QString*reason=nullptr);
// Batch A/B source graph inspection only. Caller must prove the COMPLETE
// public provider/native/allocation union before private applicability and
// before installing any exact dispatch membership. This API grants no entry.
bool proveAIActivateGetterFunction(Room&,lua_State*,int functionIndex,QString*reason=nullptr);
// One lexical, source-only traversal of a complete caller-supplied family.
// Shared nodes are visited once within THIS inspection; nothing is cached or
// admitted between query scopes. Uses the same rows, captures and native seals.
bool proveAIActivateGetterFamily(Room&,lua_State*,const QList<LuaCallbackRef>&,
    QString*reason=nullptr);
// Separate source domain for a complete nonwriting selection callback body.
// role must match its exact pinned can_select_card/card_selection_feasible/
// can_select_target/targets_feasible descriptor. This is not a native-family,
// request, lifetime or dispatcher certificate and grants no execution entry.
bool proveAIActivateSelectionFunction(Room&,lua_State*,int functionIndex,
    const char *role,QString*reason=nullptr);
// Metadata of ALL registered correction callbacks, before holder selection.
// No player/card/mark/tag value is evaluated by this routine.
// Binding/Proto inspection only: never sufficient to preserve a ledger.
bool inspectAIXufaCorrectionBindings(Room&,lua_State*,QString*reason=nullptr);
bool collectAICorrectionCaptureTables(Room&,lua_State*,QSet<const void*>&,QString*reason=nullptr);
bool proveAIXufaCorrectionUnion(Room&,lua_State*,QString*reason=nullptr);
bool certifyAIXufaPatternsEntry(Room&,lua_State*,int nargs,QString*reason=nullptr);

// Native call-tree custody, never a Lua capability or a result oracle.
// The scope must begin BEFORE ownership/invalidity/holder filtering.
class AIXufaQueryScope {
public:
 explicit AIXufaQueryScope(Room*,const Card*card=nullptr,bool cardRequired=false,bool preserveAiNoTo=false,bool preservePriority=false,bool scalarCardOnly=false);
 ~AIXufaQueryScope();
 AIXufaQueryScope(const AIXufaQueryScope&)=delete;
 AIXufaQueryScope&operator=(const AIXufaQueryScope&)=delete;
private:
 struct Data;std::unique_ptr<Data>d;friend struct AIXufaQueryAccess;
};
// Only the fresh native Slash allocated by Player::canSlash(nullptr) is
// admitted here. Not arbitrary caller-provided zero-cost/virtual cards.
class AIXufaCanSlashCard {
public:
 AIXufaCanSlashCard(Room*,const Card*);
 ~AIXufaCanSlashCard();
private:Room*room;const Card*card;quint64 stamp=0;bool entered=false;
};
// Three exact coordinator sites: validated Xufa seed/product/rebuild only.
// Observation custody, no constructor/payment/target permission.
class AIXufaSourceCard {
public:
 AIXufaSourceCard(Room&,const AIRequest&,ServerPlayer*,const Card*,const QString&skill);
 ~AIXufaSourceCard();
 AIXufaSourceCard(const AIXufaSourceCard&)=delete;
 AIXufaSourceCard&operator=(const AIXufaSourceCard&)=delete;
private:struct Data;std::unique_ptr<Data>d;friend struct AIXufaSourceAccess;
};
// Exact five native snapshot constructors only; no Lua capability or receipt.
class AIXufaProjectionCard {
public:
 AIXufaProjectionCard(Room&,ServerPlayer*,const Card*,const QString&);
 ~AIXufaProjectionCard();
 AIXufaProjectionCard(const AIXufaProjectionCard&)=delete;
 AIXufaProjectionCard&operator=(const AIXufaProjectionCard&)=delete;
private:Room*room;const Card*card;quint64 stamp=0;bool entered=false;
};
bool certifyAIXufaQueryEntry(Room&,lua_State*,int nargs,QString*reason=nullptr);

// Native-only finite state. No Lua registry/key/actor selector is exported.
// Requires native observer fanout; no replacement of another observer.
bool initializeAIXufaChoiceRegistry(Room&,lua_State*,QString*error=nullptr);
void clearAIXufaChoiceRegistry(Room&);
// Source receipts for the actual R53 writer. Never import a registry value or
// renew a retired stamp. These replace the obsolete shadow-ledger Xufa token.
ServerPlayer *currentAIXufaChoiceWriter(Room&) noexcept;
quint64 currentAIXufaChoiceSerial(Room&) noexcept;
void retireAIXufaChoiceRegistry(Room&) noexcept;
// Call BEFORE private applicability/selection at every admitted source policy
// entry. Unknown/foreign actor and nested/reentrant entry revoke unconditionally.
// An outer operation's public actor alone is NOT proof of an inner AI's actor.
void enterAIXufaChoiceRegistryPolicy(Room&,ServerPlayer*actualActor,bool reentrant);
// Until the consumer's finite native entry coverage has been reviewed, this
// remains false. It is set only by trusted native bootstrap, never by policy.
void enableAIXufaChoiceRegistryEntryCoverage(Room&,bool complete);
QJsonObject writeAIXufaChoice(Room&,ServerPlayer*,const AIRequest&,const QString&);
QJsonObject readAIXufaChoice(Room&,ServerPlayer*,const AIRequest&);

// Constructed ONLY at the exact compiled XufaCard::use askForUseCard callsite.
class AIXufaQuestionScope final {
public:
 AIXufaQuestionScope(Room&,ServerPlayer*,const Card*,const QString&);
 ~AIXufaQuestionScope();
 AIXufaQuestionScope(const AIXufaQuestionScope&)=delete;
 AIXufaQuestionScope& operator=(const AIXufaQuestionScope&)=delete;
private:struct State;std::unique_ptr<State>d;
};
// Installed at EVERY native askForUseCardStruct entry, including unrelated/nested calls.
class AIXufaInquiryScope final {
public:
 AIXufaInquiryScope(Room&,ServerPlayer*,const QString&,const QString&,int);
 ~AIXufaInquiryScope();
 AIXufaInquiryScope(const AIXufaInquiryScope&)=delete;
 AIXufaInquiryScope& operator=(const AIXufaInquiryScope&)=delete;
private:struct State;std::unique_ptr<State>d;
};
// Called only after native makeRequest has completed its actual world projection.
void bindAIXufaQuestion(const AIRequest&,ServerPlayer*);
bool aiXufaQuestion(const AIRequest&,ServerPlayer*);
std::unique_ptr<Card> constructAIXufaFirst(const AIRequest&,ServerPlayer*,const QList<int>&,QString*);
QJsonObject issueAIXufaFirst(const AIRequest&,ServerPlayer*,AIReadonlyCardLease,
 const std::function<bool(CardUseStruct&)>&);
QJsonObject issueAIXufaSaved(const AIRequest&,ServerPlayer*,AIReadonlyCardLease,const QStringList&,
 const std::function<bool(CardUseStruct&)>&);
// Pure remembered values are revalidated and receive NEW current-request authority.
QJsonObject issueAIXufaValue(const AIRequest&,ServerPlayer*,const QJsonObject&,const QStringList&,
 const std::function<bool(CardUseStruct&)>&);
// Used only by the existing declared PreCardUsed viewer projection. Returns
// the fixed constructor list only while this exact live object's list matches.
QJsonObject readAIXufaConstructedSkills(Room&,ServerPlayer*,const Card*);
bool hasAIXufaReceipt(const AIRequest&,int);
bool applyAIXufaReceipt(const AIRequest&,ServerPlayer*,const AICardSpec&,const QStringList&,
 const std::function<bool(CardUseStruct&)>&,CardUseStruct&);

bool registerAIXufaSlotSources(Room&,lua_State*,QString*error=nullptr);
QString writeAIXufaSlot(const AIRequest&,ServerPlayer*,AIReadonlyCardLease,
 const QStringList&,const QJsonObject&,QString*error=nullptr);
struct AIXufaSlotRead {
 enum Kind {Unknown,NilPlan,FalsePlan,EmptyCard,CardPlan};Kind kind=Unknown;
 AIReadonlyCardLease lease;QStringList targets;QString error;bool targetsMissing=false,targetsFalse=false;
};
AIXufaSlotRead readAIXufaSlot(const AIRequest&,ServerPlayer*);
std::unique_ptr<Card> constructAIXufaPolicy(const AIRequest&,ServerPlayer*,
 const QString&,const QList<int>&,QString*error=nullptr);
void clearAIXufaSlots(Room&);

#endif

#ifndef AI_STRATEGY_CONTEXT_H
#define AI_STRATEGY_CONTEXT_H

#include "ai.h"
#include <QJsonObject>
#include <QVariant>
#include <QList>
#include <functional>
#include <QString>
#include <QJsonArray>
#include <QSet>
#include <QStringList>
#include <memory>

struct AIEventView;
struct AIRequest;
struct AIWorldView;
class Card;
struct CardsMoveStruct;
struct JudgeStruct;
struct lua_TableObservation;
class QVariant;
class Room;
class ServerPlayer;
struct SkillContext;

// Called only after an actual fillAG notification to this recipient. clearAG
// does not revoke remembered face knowledge; every real mapping write does.
void recordAIStrategyChoiceAGDisclosure(Room &, ServerPlayer *, const QList<int> &);
void invalidateAIStrategyChoiceAGDisclosure(Room &, int cardId);
QJsonObject readAIStrategyChoiceAGDisclosure(Room &, ServerPlayer *, int cardId);
void projectAIStrategyChoiceContext(AIRequest &, Room &, ServerPlayer *, const QVariant &,
    QJsonObject &, const std::function<QJsonObject(const Card *)> &);

class AIChoiceContinuityInquiry {
public:
 AIChoiceContinuityInquiry(Room &,const QString &kind=QString(),ServerPlayer *viewer=nullptr,const QString &reason=QString());
 ~AIChoiceContinuityInquiry();
 AIChoiceContinuityInquiry(const AIChoiceContinuityInquiry&)=delete;
 AIChoiceContinuityInquiry&operator=(const AIChoiceContinuityInquiry&)=delete;
 Room *room;QString kind,reason;ServerPlayer *viewer;quint64 serial;
 QJsonObject inherited;
 AIChoiceContinuityInquiry *previous;
};
void invalidateAIChoiceContinuity(Room &);
void interruptAIChoiceContinuity(Room &);
void interruptAIChoiceContinuityAsync(Room &) noexcept;
void beginAIChoiceContinuityPolicy(Room &,const QString &viewer);
void observeAIChoiceContinuityEvent(Room &,const QString &viewer,const QString &actor);
void initializeAIChoiceContinuityRoots(Room &,bool isolated,bool valid,const QJsonObject &initialNil,quint64 xufaTransportStamp=0,quint64 xufaGenerationStamp=0);
void observeAIChoiceContinuityTable(Room &,int domain,bool isolated,const lua_TableObservation &) noexcept;
void observeAIChoiceContinuityDecision(Room &,const AIRequest &);
QJsonObject queryAIChoiceContinuity(Room &,ServerPlayer *,const AIRequest &,const QJsonObject &);
ServerPlayer*currentAIXufaContinuityWriter(Room&) noexcept;
// Native-only monotonic reached-write identity. Zero means unknown; no values
// or epoch exposed to policy. A later same-owner write is a different source.
quint64 currentAIXufaWriteSerial(Room&) noexcept;
void interruptAIChoiceContinuityExceptXufa(Room&);
void interruptAIXufaContinuity(Room&);

// Only the current named inquiry receives its source-specific observed data.
void projectAIConstructionContext(AIRequest &, Room &, const ServerPlayer *,
    const std::function<QJsonObject(const Card *)> &describeCard);

// Fixed-source readonly constructors, not native conversion products. The
// caller owns cloning/evaluation and must never use this match to issue a
// payment ticket. Exact request, raw printing and constructor fields matter.
bool aiAuditedConstructionQuery(const AIRequest &request, const ServerPlayer *viewer,
                               const QJsonObject &spec, QString *error = nullptr);
// Existing skill-action resolver still performs canSelect/createCard validation.
// -1 means not admitted; bit 0 means the source supplies material costs.
int aiAuditedConstructionSourcePrototype(const AIRequest &request, const ServerPlayer *viewer,
                                        const QString &skill, const QString &prototype);
// MR_zhizhe selects one owner hand material but emits a zero-subcard product.
// The resolver must still run the live V2 selection/factory and persist these
// selection IDs separately from product costs in its native-only receipt.
bool aiAuditedNonconsumingSelection(const AIRequest &, const ServerPlayer *,
    const QString &skill, const QJsonObject &product, const QJsonArray &selectedMaterials,
    QString *error = nullptr);
// Public engine definition metadata; never queries an opponent's skill state.
void projectAIConstructionFacts(AIWorldView &world);

// Scope is a trusted server identity, never a Lua value or Room seat name.
// No default path/profile is chosen here. In particular this service does not
// consult HOME or import the unpartitioned legacy AiData into a viewer profile.
struct AIStrategyHistorySnapshot {
    bool known = false;
    QJsonObject data;
    QString revision;
    QString error;
};

class AIStrategyHistoryWrite {
public:
    ~AIStrategyHistoryWrite();
    AIStrategyHistoryWrite(const AIStrategyHistoryWrite &) = delete;
    AIStrategyHistoryWrite &operator=(const AIStrategyHistoryWrite &) = delete;
    // Call only after the containing event has committed successfully. Until
    // then destroying this object cancels the unpublished write and unlocks it.
    bool commit(QString *error = nullptr);
    QString revision() const;
private:
    struct Impl;
    explicit AIStrategyHistoryWrite(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
    friend class AIStrategyHistoryStore;
    friend std::unique_ptr<AIStrategyHistoryWrite> prepareAIStrategyLegacyArchive(
        const QString &, const QString &, QString *, const QString &);
};

class AIStrategyHistoryStore {
public:
    // rootDirectory must already exist; no implicit creation outside an
    // explicitly configured location. Existing symlink targets are rejected.
    AIStrategyHistoryStore(const QString &rootDirectory, const QString &trustedScope);
    AIStrategyHistorySnapshot read(const QSet<QString> &allowedSkills,
                                  const QSet<QString> &allowedCardNames) const;
    // Replacement is only for currently projected keys. Nonprojected prior
    // data survives; values outside these native allowlists are rejected.
    // CAS and a thread/process lock reject concurrent lost updates explicitly.
    std::unique_ptr<AIStrategyHistoryWrite> prepare(
        const QString &expectedRevision, const QJsonObject &visibleReplacement,
        const QSet<QString> &allowedSkills, const QSet<QString> &allowedCardNames,
        QString *error = nullptr) const;
    QString error() const { return m_error; }
    // Native snapshot binding: reject a later configuration change before
    // preparing data collected under a different trusted identity.
    QString scopeId() const { return m_scopeHash; }
private:
    QString m_root, m_scopeHash, m_error;
};

struct AIStrategyLegacyHistory {
    bool valid = false;
    QJsonObject data;
    QString error;
    QStringList sourceFiles;
    QStringList warnings;
};

// Read-only inspection. Valid nonempty AiData wins over the four old category
// files. The Engine's AiDataStore rejects malformed JSON before the Lua caller,
// so that case takes the legacy branch and records a warning. Numeric strings are normalized
// for the typed contract. Unattributed samples are NEVER autoimported.
// Packaged deployments can read modern AiData from userData while the four old
// files remain under assetRoot. The optional trusted path models that split.
AIStrategyLegacyHistory inspectAIStrategyLegacyHistory(const QString &legacyDirectory,
                                                     const QString &modernAiDataPath = QString());

// Byte-for-byte content-addressed recovery archive, independent of all viewer
// profiles. The staged write is published only when its transaction commits.
// Its manifest contains validated parsed data for later explicit review/import.
std::unique_ptr<AIStrategyHistoryWrite> prepareAIStrategyLegacyArchive(
    const QString &archiveDirectory, const QString &legacyDirectory,
    QString *error = nullptr, const QString &modernAiDataPath = QString());

// Native configuration determines identity; Lua cannot choose a profile or path.
void observeAIStrategyHistory(AIWorldView &, Room &, const AIEventView &);
void projectAIStrategyHistory(AIWorldView &, Room &, ServerPlayer *);
std::unique_ptr<AIStrategyHistoryWrite> prepareAIStrategyHistory(
    const AIWorldView &, Room &, const QString &revision, const QJsonObject &data, QString *error);

// Existing normal judge projection, shared by audited named current questions.
// Caller must prove this judge belongs to the current authorized inquiry.
QJsonObject projectAIStrategyJudgeContext(const AIRequest &request, Room &room,
    ServerPlayer *viewer, const JudgeStruct *judge,
    const std::function<QJsonObject(const Card *)> &publicCard);

// Fixed audited questions only. publicCard must redact undisclosed backing IDs.
void projectAIStrategyResponseContext(AIRequest &, Room &, ServerPlayer *,
    const QVariant &, QJsonObject &context,
    const std::function<QJsonObject(const Card *)> &publicCard);
void projectAIStrategyResponseEventContext(AIWorldView &, Room &, ServerPlayer *,
    const AIEventView &, const std::function<QJsonObject(const Card *)> &publicCard);

// Surround the actual V2 effect callback, including unaudited nested callbacks.
// Only commit after a normal callback return; destruction never infers success.
class AIResponseObservationScope {
public:
    AIResponseObservationScope(Room &, const QString &skill, ServerPlayer *actor,
                               int originalEvent, const SkillContext &);
    ~AIResponseObservationScope();
    void commit();
    AIResponseObservationScope(const AIResponseObservationScope &) = delete;
    AIResponseObservationScope &operator=(const AIResponseObservationScope &) = delete;
private:
    struct Data;
    std::unique_ptr<Data> d;
};
// Call at successful native notification boundaries, with its actual audience.
// Empty audience denotes the actual all-player broadcast, not an unknown list.
void recordAIResponseObservedMark(Room &, ServerPlayer *, const QString &, int,
                                 const QList<ServerPlayer *> &audience = {});
void recordAIResponseObservedPlayerFlag(Room &, ServerPlayer *, const QString &,
                                       const QList<ServerPlayer *> &audience = {});
// Only a normally observed fresh-game GameReady may establish initial negatives.
void initializeAIResponseObservations(Room &, ServerPlayer *observer, bool freshGame);
void projectAIResponseObservations(AIWorldView &, Room &, ServerPlayer *observer);
// Caller already holds a lawful card descriptor; this grants no card face/ID.
// Unknown returns an empty object. The opaque key is never an effective card ID.
QJsonObject readAIResponseCardObservation(Room &, ServerPlayer *observer, const Card *);

// Executes only the three audited public-catalog calls at source call time.
// It consumes the same gameplay shuffle as the donor; it never reads live piles.
QJsonObject resolveAIResponseCatalogue(Room &, ServerPlayer *, const AIRequest &, const QJsonObject &);

// Current sanitized Dying observer snapshot only. No persistent observation.
void projectAIDapoEvent(AIWorldView &, Room &, ServerPlayer *viewer,
    ServerPlayer *eventPlayer, const AIEventView &, const QVariant &);

bool aiSaodongConstruction(const AIRequest &, const ServerPlayer *, const QJsonObject &);

// Fixed package questions only. Reads no arbitrary tag selected by Lua and
// never reveals card faces beyond the host's public-card serialization policy.
void projectAIStrategyRetrialContext(AIRequest &, Room &, ServerPlayer *,
    const QVariant &, QJsonObject &context,
    const std::function<QJsonObject(const Card *)> &publicCard);
// ChoiceMade is a private request-result observation, not a public broadcast.
void projectAIStrategyRetrialEventContext(AIWorldView &, Room &, ServerPlayer *,
    const AIEventView &, const std::function<QJsonObject(const Card *)> &publicCard);
// Called by Room only after the actual notification, with recipient-redacted
// moves. A loss starts the two-phase PREVIEW; only matching gain commits faces.
void recordAIStrategyRetrialPreview(Room &, ServerPlayer *recipient,
    const QList<CardsMoveStruct> &recipientMoves, bool isLostPhase,
    const std::function<QJsonObject(const Card *)> &publicCard);
// Room notification hook overload; serializes intrinsic disclosed face only.
void recordAIStrategyRetrialPreview(Room &, ServerPlayer *recipient,
    const QList<CardsMoveStruct> &recipientMoves, bool isLostPhase);
// Call for every authoritative card-location mapping change, including hidden
// moves. Invalidates existing knowledge only; never exposes the new location.
void invalidateAIStrategyRetrialPreview(Room &, int cardId);
// Pure current preview; no arbitrary tag or hidden card lookup from Lua.
QJsonObject readAIStrategyRetrialPreview(Room &, ServerPlayer *viewer, const QString &skill);

void projectAIStrategyScarletContext(AIRequest &, Room &, ServerPlayer *, const QVariant &,
    QJsonObject &, const std::function<QJsonObject(const Card *)> &);
void projectAIStrategyScarletEventContext(AIWorldView &, Room &, ServerPlayer *, const AIEventView &,
    const std::function<QJsonObject(const Card *)> &);

void projectAIStrategyEvent(AIEventView &, const AIWorldView &, Room &, ServerPlayer *observer,
                           ServerPlayer *target, const QVariant &data);
// Adds only observations previously authorized for this exact viewer. The
// native ledger belongs to the Room lifetime and never crosses into Lua.
void projectAIStrategyKnownCards(AIWorldView &, Room &, ServerPlayer *observer);

void projectAIStrategyDamage(AIWorldView &, Room &, ServerPlayer *viewer);

// Only immutable public event/world facts. The existing Room::broadcastProperty
// role -> revealRole route records actual public disclosure; no private role tag
// or additional observer ledger is needed here.
void projectAIStrategyDragonEvent(AIWorldView &world, const AIEventView &event);

#endif

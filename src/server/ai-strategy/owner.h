#ifndef AI_STRATEGY_OWNER_H
#define AI_STRATEGY_OWNER_H

#include "ai.h"
#include "engine.h"
#include "ai-owner-range-query-scope.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <memory>

struct AIRequest;
struct AIWorldView;
class Card;
struct CardLifetimeToken;
struct CardsMoveStruct;
struct LogMessage;
struct lua_State;
class QJsonObject;
class QString;
class Room;
class ServerPlayer;
struct SkillInstanceRef;

// Pure-value strategy metadata. The caller establishes the room's Engine/Lua
// runtime binding; private facts are emitted only for world.self's own viewer.
void projectAIStrategyOwner(AIWorldView &, Room &, ServerPlayer *viewer);
void projectAIStrategySkills(AIWorldView &, Room &, ServerPlayer *viewer);

// Decision-only, bounded hypothetical Slash descriptors; never action tickets.
void projectAIStrategySlashProbes(AIWorldView &world, Room &room, ServerPlayer *viewer);

// Branch-time public-deck shuffle RNG consumption; dispatcher authorizes scope.
QJsonObject consumeAIStrategyDeckCountShuffle(Room &, ServerPlayer *viewer);

// The two fixed chat patterns only; returns known-none with found=false.
QJsonObject sampleAIStrategyPatternLogName(Room &, ServerPlayer *, const QString &pattern, bool derivative = false);

QJsonObject projectAIOwnerFilteredCard(Room &, ServerPlayer *, int id, const JudgeStruct *judge = nullptr, int hypotheticalPlace = int(Player::PlaceJudge),
                                       const AICardView *disclosed = nullptr);
void recordAIStrategyNotifiedPile(Room &, ServerPlayer *, const QString &pile, const QJsonObject &locations, bool active,
                                   const QList<int> &orderedIds = {}, bool orderKnown = false);
QJsonObject readAIStrategyNotifiedPile(Room &, ServerPlayer *, const QString &pile);
QMap<QString, bool> projectAICanUseTargets(Room &, ServerPlayer *, const Card *);
void projectAIStrategyWorld(AIWorldView &, Room &, ServerPlayer *, bool eventOnly);
// Owner hypothetical filtering of descriptors the caller has already disclosed.
// Candidate IDs alone never authorize this provider.
void projectAIStrategyDisclosedFilters(AIWorldView &, Room &, ServerPlayer *,
                                       const QList<AICardView> &disclosed);
void projectAIStrategyFanProbe(AIWorldView &, Room &, ServerPlayer *, const Card *source);
void projectAIStrategyRequest(AIRequest &, Room &, ServerPlayer *);
void projectAIStrategyEvent(AIEventView &, const AIWorldView &, Room &, ServerPlayer *observer,
                           ServerPlayer *target, const QVariant &data);
bool resolveAIStrategyRandomChoice(Room &, ServerPlayer *, const AIRequest &, AIResult &);
AICardCandidateView projectAIStrategyCandidate(Room &, ServerPlayer *, const Card *, Card::HandlingMethod,
                                              AIRequest::DecisionKind, const QString &, int &);

// These checks grant only a material/disclosed-face domain. They NEVER issue a
// ticket or replace native canSelect/create/target/limitation/payment checks.
QString aiStrategyPreviewSkill(const AIRequest &request);
bool authorizeAIStrategyPreviewEvaluation(const AIRequest &, Room &, ServerPlayer *,
    int cardId, QJsonObject *disclosedFace = nullptr, QString *error = nullptr);
bool authorizeAIStrategyPreviewPayment(const AIRequest &, Room &, ServerPlayer *,
    const SkillInstanceRef &activation, const SkillInstanceRef &source,
    const QList<int> &orderedCosts, QJsonArray *disclosedFaces = nullptr, QString *error = nullptr);
// Caoduo donor submits the ORIGINAL borrowed physical card; its human V1
// viewAs clone is a different path, not an interchangeable authority product.
bool authorizeAIStrategyPreviewPhysicalProduct(const AIRequest &, Room &, ServerPlayer *,
    const SkillInstanceRef &activation, const SkillInstanceRef &source,
    int physicalId, QJsonObject *disclosedFace = nullptr, QString *error = nullptr);

// Two source call sites only. No caller supplied card IDs, filter, or seed.
// Shuffles the configured printed deck once on the gameplay RNG at call time.
QJsonObject resolveAIPublicDeck(Room &, ServerPlayer *, const AIRequest &, const QJsonObject &);

// Native movement hooks only. A notification-only pile does not call either
// function and cannot establish a witness for an actual table/hidden card.
void invalidateAIStrategyPublicDisclosure(Room &room, int cardId);
void recordAIStrategyPublicMoves(Room &room, const QList<CardsMoveStruct> &moves,
                                bool publiclyVisible);

// Frozen public face. Unknown has no card/location payload. Only a witnessed
// unchanged public mapping permits validation of already disclosed primitives;
// a different live face is never returned. Revision is disclosure provenance.
QJsonObject readAIStrategyPublicDisclosure(Room &room, ServerPlayer *viewer, int cardId);

// Only Room's all-recipient log path may record these public marriage events.
void recordAIStrategyPublicCoupleLog(Room &room, const LogMessage &log);
// A witnessed partner (or initial no-partner) must still match native state.
// Transitions before/after the public log remain unknown until they agree.
QJsonObject readAIStrategyPublicSpouse(Room &room, ServerPlayer *viewer);
void recordAIStrategyPublicKaiyuanFlag(Room &room, ServerPlayer *player, const QString &operation);
QJsonValue readAIStrategyPublicKaiyuanFlag(Room &room, ServerPlayer *player);

// Call only after the actual all-recipient CARD_FLAG notification succeeds.
// A private notification or a virtual-card setFlags call is not this witness.
// The fixed domain is Qinggang, SlashIgnoreArmor and nosjiefan-slash.
void recordAIStrategyPublicCardFlag(Room &room, const Card *card,
                                   const QString &operation);

// Merge the returned per-name booleans into a viewer's known_flags. An empty
// object means unknown, never a complete negative flag set. The caller must
// already have lawful disclosure of this card; this API grants no card face.
QJsonObject readAIStrategyPublicCardFlags(Room &room, ServerPlayer *viewer,
                                        const Card *card);

// Active decision projection scope only. Shared event serialization has no
// viewer capability and therefore receives no observation from this function.
QJsonObject readAIStrategyScopedPublicCardFlags(const Card *card);

// Call only after the real Player::setFlags(".") has completed. This records
// even an empty native flag set: viewer-local strategy flags can still exist.
void recordAIStrategyFlagsCleared(Room &room, const ServerPlayer *player);
// Actual completed GameRule -PlayClear operation, including empty mark sets.
void recordAIStrategyPlayMarksCleared(Room &room, const ServerPlayer *player);
void recordAIStrategyTurnMarksCleared(Room &room, const ServerPlayer *player);
void recordAIStrategyAllMarksCleared(Room &room, const ServerPlayer *player);
// Exact native Lol2015 clear command also retires an isolated-only shadow.
void recordAIStrategyLolWEmptyCleared(Room &room, const ServerPlayer *player);
// Fresh Room ledger projection; do not cache it with the public board.
void projectAIStrategyFlagLifecycle(AIWorldView &world, Room &room);

// Request-private candidate identities stay native. Lua receives only a token
// and count, and consumes decision RNG when it actually reaches this branch.
void projectAIStrategyRandomRequest(AIRequest &, Room &, ServerPlayer *);
// Tuixinzhifu third-pass source ordering, only when every card is already
// disclosed to this viewer. No filtering, sampling, new faces or authority.
void projectAIStrategyTuixinzhifuThirdPassRequest(AIRequest &, Room &, ServerPlayer *);
bool resolveAIStrategyRandomChoice(Room &, ServerPlayer *, const AIRequest &, AIResult &);

// Dismantlement planning samples the full hand lazily using gameplay RNG. The
// sample is a strategy fact, never a card submission or a revealed hidden ID.
void projectAIStrategyRandomPlanningRequest(AIRequest &, Room &, ServerPlayer *);
QJsonObject resolveAIStrategyRandomPlanningQuery(Room &, ServerPlayer *,
                                               const AIRequest &, const QJsonObject &);
// Only the current viewer's native tag may refresh the donor's remembered set.
// The dispatcher validates active request/revision and charges its query budget.
QJsonObject refreshAIStrategyDiscardPreferences(Room &, ServerPlayer *);

// Finite OLQimou hypothesis; never mutates marks, dispatches events or grants payment.
class AIOwnerDistanceScope final {
public:
    AIOwnerDistanceScope(const ServerPlayer *owner, int delta);
    ~AIOwnerDistanceScope();
    static bool active(const ServerPlayer *owner = nullptr);
    AIOwnerDistanceScope(const AIOwnerDistanceScope &) = delete;
    AIOwnerDistanceScope &operator=(const AIOwnerDistanceScope &) = delete;
private:
    const ServerPlayer *m_owner;
    AIMarkQueryScope m_mark;
    AIOwnerDistanceScope *m_previous;
    static thread_local AIOwnerDistanceScope *s_current;
};
bool validateAIOwnerDistance(Room &, ServerPlayer *, const AIRequest &, const QString &, int);
QJsonObject projectAIOwnerDistance(Room &, ServerPlayer *, const QString &, int);

bool aiRangeConstruction(const AIRequest&,Room&,const ServerPlayer*,const QJsonObject&);
int aiRangeExtra(const QString& source);
QJsonObject writeAIRangeQinggang(const AIRequest&,Room&,ServerPlayer*,const QJsonObject&);

bool aiRangeRoute(Room&,const AIRequest&,ServerPlayer*,const QString&,const QString&,lua_State*,int);
// Pure finite admission predicate; grants no mutation or query authority by itself.
bool aiRangeQuestion(const AIRequest&,const ServerPlayer*,const QString&);

// This authenticates a read-only hypothesis, never a skill action/payment.
bool validateAIStrategyOwnerRange(Room &, ServerPlayer *, const AIRequest &,
                                 const QString &flag, const QString &sourceKey,
                                 AIOwnerRangeQueryScope::Flag &kind);
// Caller must hold the authenticated scope. Only owner geometry is disclosed.
QJsonObject projectAIStrategyOwnerRange(Room &, ServerPlayer *, const QString &flag,
                                      const QString &sourceKey);

// Pure equality labels, scoped to one viewer and Room. Never action authority.
QString aiLegacyCardIdentity(Room &, ServerPlayer *, const std::shared_ptr<const CardLifetimeToken> &);
QString aiLegacyCardIdentity(Room &, ServerPlayer *, const Card *);
QString aiScopedLegacyCardIdentity(const Card *);
class AIIdentityProjectionScope {
public:
    AIIdentityProjectionScope(Room &, ServerPlayer *);
    ~AIIdentityProjectionScope();
private:
    Room *previousRoom; ServerPlayer *previousViewer;
};

#endif

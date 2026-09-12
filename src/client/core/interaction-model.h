#ifndef CLIENT_INTERACTION_MODEL_H
#define CLIENT_INTERACTION_MODEL_H

// Client Architecture F1: the pure data model for "server asks / client answers".
//
// This header may depend on Qt Core only. It must not contain QWidget, QGraphicsItem,
// QDialog, QQuickItem, Dashboard, RoomScene or any engine type (Card, Player, Skill).
// Interactions use only the raw values already present on the wire: card ids, player
// objectNames, option strings.
//
// The reason: one request must be able to feed the desktop RoomScene, a future text
// client, an Android client and WASM lite alike, and their only common language is
// "which option / which players / which cards".
//
// Rule truth stays on the server. This layer never judges "is this card legal"; it only
// enforces the constraints the server stated in the request (selectable set, counts,
// cancelability, deadline).

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <variant>

// The five interaction kinds migrated in F1's first vertical slice. The rest (guanxing,
// gongxin, yiji, AG, pindian, trigger order, nullification, ...) still take the old path;
// see the remaining list in docs/client-core-interaction-model.md.
enum class InteractionType
{
    None = 0,
    ChooseRole,
    ChooseGeneral,
    ChooseDirection,
    ExchangeCard,
    AskPeach,
    SkillGuanxing,
    SkillGongxin,
    SkillYiji,
    PlayCard,
    ResponseCard,
    DiscardCard,
    Choice,
    ChooseSuit,
    ChooseKingdom,
    ChoosePlayer,
    SkillInvoke,
    TriggerOrder,
    Nullification,
    ShowCard,
    AmazingGrace,
    Pindian,
    ChooseCard,
    ChooseOrder,
    ChooseRole3v3,
    Surrender,
    LuckCard,
    AskGeneral,
    ArrangeGeneral,
    QmlInteract
};

// Stable strings for snapshots/logs; they never drift with enum ordering.
QString interactionTypeName(InteractionType type);
InteractionType interactionTypeFromName(const QString &name);

// One selectable item. value is the string sent back to the server; label is only for
// view display. ClientCore only recognizes value.
struct InteractionOption
{
    QString value;
    QString label;
    bool enabled = true;
    QVariantMap metadata;

    InteractionOption() = default;
    InteractionOption(const QString &value, const QString &label = QString(), bool enabled = true);

    QJsonObject toJson() const;
};

// Card selection constraints.
//
// enumerated == true  → selectableCards is the legal set; ClientCore enforces membership.
//                       Requests where the server sent a list (AG, choose card,
//                       guanxing, ...) belong here.
// enumerated == false → the legal set cannot be enumerated on the client, because it is
//                       the result of pattern matching and pattern matching is engine
//                       rules. ClientCore does not play rule engine and guess; it only
//                       validates count, cancelability and the id range.
struct CardSelectionState
{
    bool enumerated = false;
    QList<int> selectableCards;
    QList<int> disabledCards;
    int minSelection = 0;
    int maxSelection = 0;
    QString pattern;
    int handlingMethod = -1;  // raw value of Card::HandlingMethod, -1 = unspecified

    bool isActive() const;
    QJsonObject toJson() const;
};

// Player selection constraints. selectablePlayers is always enumerated: the server
// always sends the list.
struct PlayerSelectionState
{
    QStringList selectablePlayers;
    int minSelection = 0;
    int maxSelection = 0;

    bool isActive() const;
    QJsonObject toJson() const;
};

// QtCore-only request payloads. Commands with the same interaction shape may
// share a payload type while InteractionType preserves protocol identity.
struct OptionInteractionPayload
{
    QList<InteractionOption> options;
    bool enumerated = true;
    QString tip;
    QString scheme;
};

struct PlayerInteractionPayload
{
    PlayerSelectionState selection;
};

struct SkillActivationCandidate
{
    QString skillName;
    int instanceId = 0;
};

struct CardInteractionPayload
{
    CardSelectionState selection;
    QString sourcePlayer;
    QStringList fixedTargets;
    QStringList optionalTargets;
    QString zoneFlags;
    bool handCardsVisible = false;
    // Face-down opponent hands in askForCardChosen. Count is public; faces are not.
    int hiddenHandCount = 0;
    bool includeEquip = false;
    bool cardTextAllowed = false;
    bool virtualCardAllowed = false;
    // Provider-derived eligibility is a UI hint. Only selection.enumerated
    // makes the server-provided selectableCards list authoritative.
    QList<int> suggestedCards;
    QList<int> suggestedDisabledCards;
    // Play-phase ViewAsSkill / SkillCard buttons, numbered after hand cards.
    QList<SkillActivationCandidate> skillCandidates;
};

struct RoleAssignmentInteractionPayload
{
    QString scheme;
    QStringList playerNames;
    QStringList roles;
};

enum class RearrangementMode
{
    UpOnly = 0,
    BothSides,
    DownOnly
};

QString rearrangementModeName(RearrangementMode mode);

struct RearrangeCardsInteractionPayload
{
    QList<int> cardIds;
    RearrangementMode mode = RearrangementMode::BothSides;
    int minTop = 0;
    int maxTop = 0;
    int minBottom = 0;
    int maxBottom = 0;
    bool mirrored = false;
};

struct GongxinInteractionPayload
{
    QString targetPlayer;
    QList<int> visibleCards;
    QList<int> selectableCards;
    bool allowHeartOperation = false;
};

struct YijiInteractionPayload
{
    QList<int> cardIds;
    QStringList targetPlayers;
    int minCards = 0;
    int maxCards = 0;
    int remainingCount = 0;
};

struct PindianInteractionPayload
{
    QString opponent;
    CardSelectionState selection;
    bool revealImmediately = false;
    bool hiddenUntilResolved = true;
};

struct AmazingGraceInteractionPayload
{
    CardSelectionState selection;
    QList<int> takenCards;
    bool selectable = true;
};

struct ArrangeGeneralsInteractionPayload
{
    QStringList generalNames;
    QString arrangement;
    int slotCount = 0;
};

struct TriggerOrderOption
{
    QString skillName;
    int instanceId = 0;
    QString invoker;
    QString owner;
    QString preferredTarget;
    int preferredTargetSeat = 0;
    QString responseValue;
};

struct TriggerOrderInteractionPayload
{
    QList<TriggerOrderOption> options;
};

struct ChooseOrderInteractionPayload
{
    QList<InteractionOption> options;
    int reason = 0;
};

struct CustomInteractionPayload
{
    int schemaVersion = 1;
    QString typeName;
    QString title;
    QJsonObject payload;
    QJsonObject responseSchema;
};

using InteractionPayload = std::variant<std::monostate,
    OptionInteractionPayload,
    PlayerInteractionPayload,
    CardInteractionPayload,
    RoleAssignmentInteractionPayload,
    RearrangeCardsInteractionPayload,
    GongxinInteractionPayload,
    YijiInteractionPayload,
    PindianInteractionPayload,
    AmazingGraceInteractionPayload,
    ArrangeGeneralsInteractionPayload,
    TriggerOrderInteractionPayload,
    ChooseOrderInteractionPayload,
    CustomInteractionPayload>;

enum class InteractionResponseShape
{
    None = 0,
    Option,
    Players,
    Cards,
    Assignment,
    Rearrangement,
    Distribution,
    GeneralArrangement,
    Custom
};

QString interactionResponseShapeName(InteractionResponseShape shape);

// Fully structured description of one server request.
struct InteractionRequest
{
    // Correlation ID assigned by ClientCore, monotonically increasing, starting at 1.
    // 0 = unassigned.
    quint64 requestId = 0;
    // Protocol V2 message_id 直接存入 requestId，作完整 quint64 關聯。
    InteractionType type = InteractionType::None;
    // Raw value of QSanProtocol::CommandType. ClientCore does not include protocol.h, so
    // it is stored as int.
    int command = 0;

    QString skillName;
    QString prompt;
    bool cancelable = false;

    // 0 = no deadline. deadlineMs is a monotonic millisecond value on the ClientCore clock,
    // computed by beginRequest() from timeoutMs.
    qint64 timeoutMs = 0;
    qint64 deadlineMs = 0;

    InteractionResponseShape responseSchema = InteractionResponseShape::None;
    InteractionPayload payload;

    // 只准 diagnostic／logging／display-only 資料。所有 gameplay constraint
    // 必須有 typed payload field；metadata key 由測試 allowlist 鎖定。
    QVariantMap metadata;

    bool isValid() const;
    // Which selection dimension is valid, by type; shared by snapshot and validation.
    int minSelection() const;
    int maxSelection() const;
    bool hasOption(const QString &value) const;
    const InteractionOption *option(const QString &value) const;

    template<typename T>
    const T *payloadAs() const
    {
        return std::get_if<T>(&payload);
    }

    QJsonObject toJson() const;
    // Deterministically ordered, compact JSON. QJsonObject already sorts by key, so the same
    // request yields the same byte string on every platform — the snapshot test relies on
    // this.
    QByteArray toSnapshot() const;
};

enum class InteractionResponseKind
{
    Assignment = 5,
    Rearrangement,
    Distribution,
    GeneralArrangement,
    Custom,
    None = 0,
    Cancel,   // give up / no answer. Only cancelable requests accept it
    Option,   // picked one option value
    Players,  // picked zero or more players
    Cards     // picked zero or more cards (may carry a virtual card's text)
};

QString interactionResponseKindName(InteractionResponseKind kind);

struct InteractionResponse
{
    quint64 requestId = 0;
    int command = 0;
    InteractionResponseKind kind = InteractionResponseKind::None;

    struct CancelData
    {
    };

    struct OptionData
    {
        QString value;
    };

    struct PlayerSelectionData
    {
        QStringList names;
    };

    struct CardSelectionData
    {
        QList<int> cardIds;
        QString cardText;
        QList<int> subcardIds;
        QStringList targets;
        QString activationSkillName;
        int activationSkillInstanceId = 0;
    };

    struct AssignmentData
    {
        QStringList names;
        QStringList values;
    };

    struct RearrangementData
    {
        QList<int> first;
        QList<int> second;
    };

    struct DistributionData
    {
        QList<int> cards;
        QString target;
    };

    struct GeneralArrangementData
    {
        QStringList generalNames;
    };

    struct CustomData
    {
        int schemaVersion = 1;
        QString typeName;
        QVariant value;
    };

    using Payload = std::variant<std::monostate, CancelData, OptionData,
        PlayerSelectionData, CardSelectionData, AssignmentData,
        RearrangementData, DistributionData, GeneralArrangementData, CustomData>;
    Payload payload;

    template<typename T>
    const T *payloadAs() const
    {
        return std::get_if<T>(&payload);
    }

    static InteractionResponse makeCancel(quint64 requestId);
    static InteractionResponse makeOption(quint64 requestId, const QString &value);
    static InteractionResponse makePlayers(quint64 requestId, const QStringList &names);
    static InteractionResponse makeCards(quint64 requestId, const QList<int> &ids,
        const QString &cardText = QString());
    static InteractionResponse makeAssignment(quint64 requestId,
        const QStringList &names, const QStringList &values);
    static InteractionResponse makeRearrangement(quint64 requestId,
        const QList<int> &first, const QList<int> &second);
    static InteractionResponse makeDistribution(quint64 requestId,
        const QList<int> &ids, const QString &target);
    static InteractionResponse makeGeneralArrangement(quint64 requestId,
        const QStringList &generalNames);
    static InteractionResponse makeCustom(quint64 requestId, int schemaVersion,
        const QString &typeName, const QVariant &value);

    QJsonObject toJson() const;
    QByteArray toSnapshot() const;
};

// Rejection reasons. Each one corresponds to an entry in the "ClientCore must reject"
// list of the completion criteria.
enum class InteractionRejection
{
    CommandMismatch = 17,
    MalformedResponse,
    UnsupportedInteraction,
    None = 0,
    NoActiveRequest,           // no request currently awaiting an answer
    RequestIdMismatch,         // reply's id is not the active request
    AlreadyCompleted,          // duplicate reply: this request was already answered
    RequestCancelled,          // request 已取消後再 reply
    RequestExpired,            // request 已過期
    KindMismatch,              // answer kind does not match the request
    UnknownOption,             // nonexistent option
    DisabledOption,            // exists but the server marked it non-selectable
    UnknownPlayer,             // 非 selectable player
    DuplicatePlayer,
    UnknownCard,               // 非 selectable card／id 超出值域
    DisabledCard,
    DuplicateCard,
    UnknownGeneral = 20,
    DuplicateGeneral,
    SelectionCountOutOfRange,  // selection 數量錯誤
    NotCancelable              // non-cancelable request received an empty answer
};

QString interactionRejectionName(InteractionRejection rejection);

struct InteractionValidation
{
    InteractionRejection rejection = InteractionRejection::None;
    QString detail;

    bool accepted() const { return rejection == InteractionRejection::None; }
    QString reasonName() const { return interactionRejectionName(rejection); }

    static InteractionValidation ok();
    static InteractionValidation fail(InteractionRejection rejection, const QString &detail = QString());
};

// Why a request ended without being completed by an answer.
enum class InteractionCancelReason
{
    Superseded = 0,  // the next server request arrived
    Expired,         // deadline passed
    Abandoned,       // abandoned locally (no reply is sent)
    Disconnected     // connection lost / game over
};

QString interactionCancelReasonName(InteractionCancelReason reason);

#endif

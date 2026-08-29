#ifndef CLIENT_INTERACTION_MODEL_H
#define CLIENT_INTERACTION_MODEL_H

// Client Architecture F1：server 問嘢／client 答嘢嘅純資料模型。
//
// 呢個 header 只准依賴 Qt Core。入面唔可以出現 QWidget、QGraphicsItem、
// QDialog、QQuickItem、Dashboard、RoomScene 或者任何 engine 型別（Card、
// Player、Skill）。Interaction 只用得 wire 上面已經有嘅原始值：卡 id、玩家
// objectName、option 字串。
//
// 咁樣做嘅原因係：一個 request 要可以同時餵得起 desktop RoomScene、將來嘅
// text client、Android client 同 WASM lite，而佢哋唯一嘅共通語言就係
// 「揀邊個 option / 邊啲玩家 / 邊啲卡」。
//
// 規則真相仍然喺 server。呢層唔會判斷「呢張牌合唔合法」，只會執行 server
// 喺 request 入面已經講明嘅約束（可選集合、數量、可唔可以取消、死線）。

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <variant>

// F1 第一個垂直切片遷移嘅五類 interaction。其餘（guanxing、gongxin、yiji、
// AG、pindian、trigger order、nullification……）仍然行舊路,見
// docs/client-core-interaction-model.md 嘅 remaining 清單。
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

// snapshot／log 用嘅穩定字串，唔會跟住 enum 次序漂移。
QString interactionTypeName(InteractionType type);
InteractionType interactionTypeFromName(const QString &name);

// 一個可揀項。value 係送返 server 嗰個字串,label 淨係畀 view 顯示。
// ClientCore 只認 value。
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

// 卡牌選擇約束。
//
// enumerated == true  → selectableCards 就係合法集合,ClientCore 會強制檢查
//                       成員資格。server 有send過清單嘅 request（AG、
//                       choose card、guanxing……）屬呢類。
// enumerated == false → 合法集合喺 client 層無法枚舉,因為佢係 pattern 配對
//                       嘅結果,而 pattern 配對係 engine 規則。ClientCore
//                       唔會扮規則引擎去猜,只會驗數量、取消權同 id 值域。
struct CardSelectionState
{
    bool enumerated = false;
    QList<int> selectableCards;
    QList<int> disabledCards;
    int minSelection = 0;
    int maxSelection = 0;
    QString pattern;
    int handlingMethod = -1;  // Card::HandlingMethod 嘅原始值,-1 = 未指定

    bool isActive() const;
    QJsonObject toJson() const;
};

// 玩家選擇約束。selectablePlayers 永遠係枚舉嘅：server 一定會 send 清單。
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

struct CardInteractionPayload
{
    CardSelectionState selection;
    QString sourcePlayer;
    QStringList fixedTargets;
    QStringList optionalTargets;
    QString zoneFlags;
    bool handCardsVisible = false;
    bool includeEquip = false;
    bool cardTextAllowed = false;
    bool virtualCardAllowed = false;
    // Provider-derived eligibility is a UI hint. Only selection.enumerated
    // makes the server-provided selectableCards list authoritative.
    QList<int> suggestedCards;
    QList<int> suggestedDisabledCards;
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
    QString legacyQmlPath;
    bool legacy = false;
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

// 一個 server request 嘅完整結構化描述。
struct InteractionRequest
{
    // ClientCore 派嘅 correlation ID,單調遞增,由 1 開始。0 = 未編號。
    quint64 requestId = 0;
    // 對應嘅 protocol packet globalSerial。留住係為咗對數同診斷；
    // reply 嘅 localSerial 仍然由 Client 依原有路徑填,wire 冇改。
    uint serverSerial = 0;
    InteractionType type = InteractionType::None;
    // QSanProtocol::CommandType 嘅原始值。ClientCore 唔 include protocol.h,
    // 所以用 int 儲住。
    int command = 0;

    QString skillName;
    QString prompt;
    bool cancelable = false;

    // 0 = 冇死線。deadlineMs 係 ClientCore clock 嘅單調毫秒數,由 beginRequest()
    // 按 timeoutMs 計出嚟。
    qint64 timeoutMs = 0;
    qint64 deadlineMs = 0;

    InteractionResponseShape responseSchema = InteractionResponseShape::None;
    InteractionPayload payload;

    // 只准 diagnostic／logging／display-only 資料。所有 gameplay constraint
    // 必須有 typed payload field；metadata key 由測試 allowlist 鎖定。
    QVariantMap metadata;

    bool isValid() const;
    // 按 type 決定邊條 selection 維度有效,畀 snapshot 同驗證共用。
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
    // 穩定排序、compact 嘅 JSON。QJsonObject 本身按 key 排序,所以同一個
    // request 喺任何平台都出同一串 bytes —— snapshot test 靠呢個。
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
    Cancel,   // 放棄／唔答。只有 cancelable request 收
    Option,   // 揀咗一個 option value
    Players,  // 揀咗零個或多個玩家
    Cards     // 揀咗零張或多張卡(可以帶 virtual card 嘅 text)
};

QString interactionResponseKindName(InteractionResponseKind kind);

struct InteractionResponse
{
    quint64 requestId = 0;
    uint serverSerial = 0;
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

// 拒絕原因。每一個都對應完成標準入面「ClientCore 必須拒絕」嗰張清單。
enum class InteractionRejection
{
    ServerSerialMismatch = 16,
    CommandMismatch,
    MalformedResponse,
    UnsupportedInteraction,
    None = 0,
    NoActiveRequest,           // 而家冇 request 等緊答
    RequestIdMismatch,         // reply 嘅 id 唔係 active request
    AlreadyCompleted,          // duplicate reply:呢個 request 已經答咗
    RequestCancelled,          // request 已取消後再 reply
    RequestExpired,            // request 已過期
    KindMismatch,              // 答案種類同 request 對唔上
    UnknownOption,             // 唔存在嘅 option
    DisabledOption,            // 存在但 server 標咗唔可揀
    UnknownPlayer,             // 非 selectable player
    DuplicatePlayer,
    UnknownCard,               // 非 selectable card／id 超出值域
    DisabledCard,
    DuplicateCard,
    UnknownGeneral = 20,
    DuplicateGeneral,
    SelectionCountOutOfRange,  // selection 數量錯誤
    NotCancelable              // 唔准取消嘅 request 收到空答案
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

// request 唔係由答案完成嗰陣嘅原因。
enum class InteractionCancelReason
{
    Superseded = 0,  // 下一個 server request 到咗
    Expired,         // 過咗死線
    Abandoned,       // 本機主動放棄(唔會送 reply)
    Disconnected     // 連線斷咗／局終
};

QString interactionCancelReasonName(InteractionCancelReason reason);

#endif

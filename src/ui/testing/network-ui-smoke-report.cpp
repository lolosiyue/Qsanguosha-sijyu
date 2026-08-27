#include "network-ui-smoke-report.h"

#include <ctime>

#include <QHash>
#include <QJsonDocument>
#include <QVariant>

#include "protocol.h"

namespace {

const int kSchemaVersion = 1;

// 一局真實網絡局（含 AI 思考時間）通常喺 2-5 分鐘之內；預設 10 分鐘留足夠餘裕，
// 但一定有界。下限只係擋手誤，負向契約測試需要一個一定會觸發的極短 timeout。
const int kDefaultTimeoutMs = 600000;
const int kMinimumTimeoutMs = 1000;
const int kMaximumTimeoutMs = 3600000;

// 單一 server request 未能經 UI 回覆嘅容忍時間。超過就切 trustee，令對局一定
// 行得完；切換本身會記入 report，唔會靜靜當冇事。
const int kDefaultStallMs = 20000;
const int kMinimumStallMs = 1000;
const int kMaximumStallMs = 600000;

QString flagValue(const QStringList &arguments, const QString &flag, bool *found)
{
    *found = false;
    const QString prefix = flag + QLatin1Char('=');
    for (int i = 0; i < arguments.size(); ++i) {
        const QString &argument = arguments.at(i);
        if (argument == flag) {
            *found = true;
            return i + 1 < arguments.size() ? arguments.at(i + 1) : QString();
        }
        if (argument.startsWith(prefix)) {
            *found = true;
            return argument.mid(prefix.size());
        }
    }
    return QString();
}

bool parseBoundedInt(const QStringList &arguments, const char *flag, int minimum,
    int maximum, int defaultValue, int *value, QString *error)
{
    *value = defaultValue;
    if (error)
        error->clear();

    bool found = false;
    const QString raw = flagValue(arguments, QLatin1String(flag), &found);
    if (!found)
        return true;

    bool ok = false;
    const int parsed = raw.toInt(&ok);
    if (!ok) {
        if (error)
            *error = QStringLiteral("%1 expects an integer, got '%2'")
                .arg(QLatin1String(flag), raw);
        return false;
    }
    if (parsed < minimum || parsed > maximum) {
        if (error)
            *error = QStringLiteral("%1 must be within [%2, %3], got %4")
                .arg(QLatin1String(flag))
                .arg(minimum)
                .arg(maximum)
                .arg(parsed);
        return false;
    }
    *value = parsed;
    return true;
}

QString compactJson(const QJsonObject &payload)
{
    return QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

// server request → 契約互動名。只列真正需要 client 回覆嘅 interaction command
// （Client::m_interactions 的成員），其餘一律回空字串。
const QHash<int, QString> &interactionNames()
{
    using namespace QSanProtocol;
    static const QHash<int, QString> names = {
        { S_COMMAND_RESPONSE_CARD, QStringLiteral("ask_for_card") },
        { S_COMMAND_ASK_PEACH, QStringLiteral("ask_for_card") },
        { S_COMMAND_NULLIFICATION, QStringLiteral("ask_for_card") },
        { S_COMMAND_MULTIPLE_CHOICE, QStringLiteral("ask_for_choice") },
        { S_COMMAND_CHOOSE_PLAYER, QStringLiteral("ask_for_player_chosen") },
        { S_COMMAND_INVOKE_SKILL, QStringLiteral("ask_for_skill_invoke") },
        { S_COMMAND_PLAY_CARD, QStringLiteral("play_phase") },
        { S_COMMAND_DISCARD_CARD, QStringLiteral("ask_for_discard") },
        { S_COMMAND_EXCHANGE_CARD, QStringLiteral("ask_for_exchange") },
        { S_COMMAND_CHOOSE_GENERAL, QStringLiteral("choose_general") },
        { S_COMMAND_CHOOSE_CARD, QStringLiteral("ask_for_card_chosen") },
        { S_COMMAND_CHOOSE_SUIT, QStringLiteral("ask_for_suit") },
        { S_COMMAND_CHOOSE_KINGDOM, QStringLiteral("ask_for_kingdom") },
        { S_COMMAND_CHOOSE_ROLE, QStringLiteral("ask_for_assign") },
        { S_COMMAND_CHOOSE_ROLE_3V3, QStringLiteral("ask_for_role_3v3") },
        { S_COMMAND_CHOOSE_DIRECTION, QStringLiteral("ask_for_direction") },
        { S_COMMAND_CHOOSE_ORDER, QStringLiteral("ask_for_order") },
        { S_COMMAND_AMAZING_GRACE, QStringLiteral("ask_for_ag") },
        { S_COMMAND_SKILL_YIJI, QStringLiteral("ask_for_yiji") },
        { S_COMMAND_SKILL_GUANXING, QStringLiteral("ask_for_guanxing") },
        { S_COMMAND_SKILL_GONGXIN, QStringLiteral("ask_for_gongxin") },
        { S_COMMAND_TRIGGER_ORDER, QStringLiteral("ask_for_trigger_order") },
        { S_COMMAND_SHOW_CARD, QStringLiteral("ask_for_card_show") },
        { S_COMMAND_PINDIAN, QStringLiteral("ask_for_pindian") },
        { S_COMMAND_SURRENDER, QStringLiteral("ask_for_surrender") },
        { S_COMMAND_LUCK_CARD, QStringLiteral("ask_for_luck_card") },
        { S_COMMAND_ASK_GENERAL, QStringLiteral("ask_for_general_3v3") },
        { S_COMMAND_ARRANGE_GENERAL, QStringLiteral("ask_for_arrange_general") },
        { S_COMMAND_QML_INTERACT, QStringLiteral("ask_for_qml") },
    };
    return names;
}

} // namespace

const char *const NetworkUiSmokeReport::StageMarker = "NETWORK_UI_STAGE";
const char *const NetworkUiSmokeReport::ResultMarker = "NETWORK_UI_RESULT";

const char *const NetworkUiSmokeReport::StageConnected = "connected";
const char *const NetworkUiSmokeReport::StageSignedUp = "signed_up";
const char *const NetworkUiSmokeReport::StageRoomScene = "room_scene";
const char *const NetworkUiSmokeReport::StageDashboard = "dashboard";
const char *const NetworkUiSmokeReport::StageGeneralSelected = "general_selected";
const char *const NetworkUiSmokeReport::StageGameStarted = "game_started";
const char *const NetworkUiSmokeReport::StageGameOver = "game_over";
const char *const NetworkUiSmokeReport::StageShutdown = "shutdown";

const char *const NetworkUiSmokeReport::FlagNetworkUiSmoke = "--network-ui-smoke";
const char *const NetworkUiSmokeReport::FlagResultPath = "--network-ui-smoke-result";
const char *const NetworkUiSmokeReport::FlagTimeoutMs = "--network-ui-smoke-timeout-ms";
const char *const NetworkUiSmokeReport::FlagStallMs = "--network-ui-smoke-stall-ms";
const char *const NetworkUiSmokeReport::FlagScreenshotPath = "--network-ui-smoke-screenshot";

const char *const NetworkUiSmokeReport::ReasonOk = "ok";
const char *const NetworkUiSmokeReport::ReasonStageFailed = "stage_failed";
const char *const NetworkUiSmokeReport::ReasonTimeout = "timeout";
const char *const NetworkUiSmokeReport::ReasonDisconnected = "disconnected";
const char *const NetworkUiSmokeReport::ReasonInteractionStalled = "interaction_stalled";

const char *const NetworkUiSmokeReport::ActionPlayCard = "play_card";
const char *const NetworkUiSmokeReport::ActionSelectTarget = "select_target";
const char *const NetworkUiSmokeReport::ActionFinishPhase = "finish_phase";
const char *const NetworkUiSmokeReport::ActionChooseOption = "choose_option";
const char *const NetworkUiSmokeReport::ActionChooseCard = "choose_card";
const char *const NetworkUiSmokeReport::ActionChoosePlayer = "choose_player";
const char *const NetworkUiSmokeReport::ActionInvokeSkill = "invoke_skill";
const char *const NetworkUiSmokeReport::ActionDecline = "decline";
const char *const NetworkUiSmokeReport::ActionTrusteeFallback = "trustee_fallback";

int NetworkUiSmokeReport::schemaVersion()
{
    return kSchemaVersion;
}

int NetworkUiSmokeReport::defaultTimeoutMs()
{
    return kDefaultTimeoutMs;
}

int NetworkUiSmokeReport::minimumTimeoutMs()
{
    return kMinimumTimeoutMs;
}

int NetworkUiSmokeReport::maximumTimeoutMs()
{
    return kMaximumTimeoutMs;
}

int NetworkUiSmokeReport::defaultStallMs()
{
    return kDefaultStallMs;
}

int NetworkUiSmokeReport::minimumStallMs()
{
    return kMinimumStallMs;
}

int NetworkUiSmokeReport::maximumStallMs()
{
    return kMaximumStallMs;
}

QStringList NetworkUiSmokeReport::stageOrder()
{
    return QStringList{
        QLatin1String(StageConnected),
        QLatin1String(StageSignedUp),
        QLatin1String(StageRoomScene),
        QLatin1String(StageDashboard),
        QLatin1String(StageGeneralSelected),
        QLatin1String(StageGameStarted),
        QLatin1String(StageGameOver),
        QLatin1String(StageShutdown)
    };
}

bool NetworkUiSmokeReport::isKnownStage(const QString &stage)
{
    return stageOrder().contains(stage);
}

bool NetworkUiSmokeReport::isRequested(const QStringList &arguments)
{
    return arguments.contains(QLatin1String(FlagNetworkUiSmoke));
}

bool NetworkUiSmokeReport::parseTimeoutMs(const QStringList &arguments, int *timeoutMs,
    QString *error)
{
    return parseBoundedInt(arguments, FlagTimeoutMs, kMinimumTimeoutMs, kMaximumTimeoutMs,
        kDefaultTimeoutMs, timeoutMs, error);
}

bool NetworkUiSmokeReport::parseStallMs(const QStringList &arguments, int *stallMs,
    QString *error)
{
    return parseBoundedInt(arguments, FlagStallMs, kMinimumStallMs, kMaximumStallMs,
        kDefaultStallMs, stallMs, error);
}

QString NetworkUiSmokeReport::parseResultPath(const QStringList &arguments)
{
    bool found = false;
    const QString value = flagValue(arguments, QLatin1String(FlagResultPath), &found);
    return found ? value : QString();
}

QString NetworkUiSmokeReport::parseScreenshotPath(const QStringList &arguments)
{
    bool found = false;
    const QString value = flagValue(arguments, QLatin1String(FlagScreenshotPath), &found);
    return found ? value : QString();
}

QString NetworkUiSmokeReport::interactionName(int commandType)
{
    return interactionNames().value(commandType);
}

QStringList NetworkUiSmokeReport::knownInteractionNames()
{
    QStringList names = interactionNames().values();
    names.removeDuplicates();
    names.sort();
    return names;
}

QStringList NetworkUiSmokeReport::knownActionNames()
{
    return QStringList{
        QLatin1String(ActionPlayCard),
        QLatin1String(ActionSelectTarget),
        QLatin1String(ActionFinishPhase),
        QLatin1String(ActionChooseOption),
        QLatin1String(ActionChooseCard),
        QLatin1String(ActionChoosePlayer),
        QLatin1String(ActionInvokeSkill),
        QLatin1String(ActionDecline),
        QLatin1String(ActionTrusteeFallback)
    };
}

QJsonObject NetworkUiSmokeReport::stagePayload(const QString &stage, bool ok,
    const QJsonObject &details)
{
    QJsonObject payload = details;
    payload.insert(QStringLiteral("schema_version"), kSchemaVersion);
    payload.insert(QStringLiteral("stage"), stage);
    payload.insert(QStringLiteral("ok"), ok);
    return payload;
}

QJsonObject NetworkUiSmokeReport::resultPayload(bool ok, const QString &stage,
    const QString &error, ExitCode exitCode, const QJsonObject &details)
{
    QJsonObject payload = details;
    payload.insert(QStringLiteral("schema_version"), kSchemaVersion);
    payload.insert(QStringLiteral("ok"), ok);
    payload.insert(QStringLiteral("stage"), stage);
    payload.insert(QStringLiteral("exit_code"), static_cast<int>(exitCode));
    if (ok) {
        payload.insert(QStringLiteral("reason"), QLatin1String(ReasonOk));
    } else {
        const char *reason = ReasonStageFailed;
        if (exitCode == Timeout)
            reason = ReasonTimeout;
        else if (exitCode == Disconnected)
            reason = ReasonDisconnected;
        else if (exitCode == InteractionFailed)
            reason = ReasonInteractionStalled;
        payload.insert(QStringLiteral("reason"), QLatin1String(reason));
        payload.insert(QStringLiteral("error"), error);
    }
    return payload;
}

QString NetworkUiSmokeReport::stageLine(const QString &stage, bool ok,
    const QJsonObject &details)
{
    return QLatin1String(StageMarker) + QLatin1Char(' ')
        + compactJson(stagePayload(stage, ok, details));
}

QString NetworkUiSmokeReport::resultLine(bool ok, const QString &stage,
    const QString &error, ExitCode exitCode, const QJsonObject &details)
{
    return QLatin1String(ResultMarker) + QLatin1Char(' ')
        + compactJson(resultPayload(ok, stage, error, exitCode, details));
}

NetworkUiSmokeReport::ExitCode NetworkUiSmokeReport::exitCodeForFailedStage(const QString &stage)
{
    if (stage == QLatin1String(StageConnected))
        return ConnectFailed;
    if (stage == QLatin1String(StageSignedUp))
        return SignupFailed;
    if (stage == QLatin1String(StageRoomScene))
        return RoomSceneFailed;
    if (stage == QLatin1String(StageDashboard))
        return DashboardFailed;
    if (stage == QLatin1String(StageGeneralSelected))
        return GeneralSelectionFailed;
    if (stage == QLatin1String(StageGameStarted))
        return GameStartFailed;
    if (stage == QLatin1String(StageGameOver))
        return GameOverNotReached;
    if (stage == QLatin1String(StageShutdown))
        return InternalError;
    if (stage == QLatin1String("arguments"))
        return InvalidArguments;
    return InternalError;
}

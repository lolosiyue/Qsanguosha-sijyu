// Linux GUI M2 network UI smoke 的 marker／exit code／command 對照契約測試。
//
// 只依賴 Qt Core：契約本身唔應該要開 QApplication 或者起一局真網絡局先驗到，
// CI 亦要喺 server-only runner 上照跑。真正的網絡對局由
// tools/autotest/gui_network_smoke.py 喺 Xvfb 下驗證。
#include "network-ui-smoke-report.h"

#include <ctime>

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>

#include "protocol.h"

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (condition) {
        printf("PASS %s\n", what);
        return;
    }
    printf("FAIL %s\n", what);
    ++failures;
}

QJsonObject parseMarker(const QString &line, const QString &expectedMarker)
{
    const QString prefix = expectedMarker + QLatin1Char(' ');
    if (!line.startsWith(prefix))
        return {};
    QJsonParseError error{};
    const QJsonDocument document =
        QJsonDocument::fromJson(line.mid(prefix.size()).toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return {};
    return document.object();
}

void testMarkerNames()
{
    check(QLatin1String(NetworkUiSmokeReport::StageMarker)
            == QLatin1String("NETWORK_UI_STAGE"),
        "stage marker token is stable");
    check(QLatin1String(NetworkUiSmokeReport::ResultMarker)
            == QLatin1String("NETWORK_UI_RESULT"),
        "result marker token is stable");
    check(QLatin1String(NetworkUiSmokeReport::FlagNetworkUiSmoke)
            == QLatin1String("--network-ui-smoke"),
        "network UI smoke CLI flag is stable");
    check(QLatin1String(NetworkUiSmokeReport::FlagResultPath)
            == QLatin1String("--network-ui-smoke-result"),
        "result path CLI flag is stable");
    check(NetworkUiSmokeReport::schemaVersion() == 1, "result schema version is 1");
}

void testStageOrder()
{
    const QStringList stages = NetworkUiSmokeReport::stageOrder();
    const QStringList expected{
        QStringLiteral("connected"), QStringLiteral("signed_up"),
        QStringLiteral("room_scene"), QStringLiteral("dashboard"),
        QStringLiteral("general_selected"), QStringLiteral("game_started"),
        QStringLiteral("game_over"), QStringLiteral("shutdown")
    };
    // RoomScene 喺 setup 收到即刻由 enterRoom() 建立，早過選將請求；呢個次序係
    // 產品真實流程，唔可以為咗遷就文件而調轉。
    check(stages == expected, "stage order matches the real network startup path");
    check(stages.indexOf(QStringLiteral("room_scene"))
            < stages.indexOf(QStringLiteral("general_selected")),
        "RoomScene is built before the general-selection request arrives");
    check(NetworkUiSmokeReport::isKnownStage(QStringLiteral("dashboard")),
        "dashboard is a known stage");
    check(!NetworkUiSmokeReport::isKnownStage(QStringLiteral("nonsense")),
        "unknown stage names are rejected");
}

void testStageLineSchema()
{
    const QString line = NetworkUiSmokeReport::stageLine(QStringLiteral("room_scene"), true,
        QJsonObject{{QStringLiteral("photo_count"), 4}});
    const QJsonObject payload =
        parseMarker(line, QLatin1String(NetworkUiSmokeReport::StageMarker));
    check(!payload.isEmpty(), "stage line is one marker token plus compact JSON");
    check(payload.value(QStringLiteral("schema_version")).toInt() == 1,
        "stage payload carries the schema version");
    check(payload.value(QStringLiteral("stage")).toString() == QLatin1String("room_scene"),
        "stage payload names the stage");
    check(payload.value(QStringLiteral("ok")).toBool(), "stage payload carries ok");
    check(payload.value(QStringLiteral("photo_count")).toInt() == 4,
        "stage payload keeps caller-supplied details");
    check(!line.contains(QLatin1Char('\n')), "stage line stays on a single line");
}

void testResultLineSchema()
{
    const QString passLine = NetworkUiSmokeReport::resultLine(true,
        QStringLiteral("shutdown"), QString(), NetworkUiSmokeReport::Passed);
    const QJsonObject pass =
        parseMarker(passLine, QLatin1String(NetworkUiSmokeReport::ResultMarker));
    check(pass.value(QStringLiteral("ok")).toBool(), "passing result reports ok=true");
    check(pass.value(QStringLiteral("exit_code")).toInt() == 0,
        "passing result reports exit code 0");
    check(pass.value(QStringLiteral("reason")).toString() == QLatin1String("ok"),
        "passing result reports reason=ok");
    check(!pass.contains(QStringLiteral("error")),
        "passing result carries no error field");

    const QJsonObject failure = parseMarker(
        NetworkUiSmokeReport::resultLine(false, QStringLiteral("dashboard"),
            QStringLiteral("no dashboard"), NetworkUiSmokeReport::DashboardFailed),
        QLatin1String(NetworkUiSmokeReport::ResultMarker));
    check(failure.value(QStringLiteral("reason")).toString()
            == QLatin1String("stage_failed"),
        "stage failures report reason=stage_failed");
    check(failure.value(QStringLiteral("error")).toString() == QLatin1String("no dashboard"),
        "stage failures carry the error text");

    // timeout／斷線／互動卡死同「stage 本身失敗」共用 stage 名，唯一的分辨方法
    // 就係 reason；分唔開就等於 CI 睇唔出係邊種故障。
    const QJsonObject timeout = parseMarker(
        NetworkUiSmokeReport::resultLine(false, QStringLiteral("game_over"),
            QStringLiteral("timed out"), NetworkUiSmokeReport::Timeout),
        QLatin1String(NetworkUiSmokeReport::ResultMarker));
    check(timeout.value(QStringLiteral("reason")).toString() == QLatin1String("timeout"),
        "timeouts report reason=timeout");

    const QJsonObject disconnected = parseMarker(
        NetworkUiSmokeReport::resultLine(false, QStringLiteral("game_over"),
            QStringLiteral("server went away"), NetworkUiSmokeReport::Disconnected),
        QLatin1String(NetworkUiSmokeReport::ResultMarker));
    check(disconnected.value(QStringLiteral("reason")).toString()
            == QLatin1String("disconnected"),
        "mid-game disconnects report reason=disconnected");

    const QJsonObject stalled = parseMarker(
        NetworkUiSmokeReport::resultLine(false, QStringLiteral("game_over"),
            QStringLiteral("no UI response"), NetworkUiSmokeReport::InteractionFailed),
        QLatin1String(NetworkUiSmokeReport::ResultMarker));
    check(stalled.value(QStringLiteral("reason")).toString()
            == QLatin1String("interaction_stalled"),
        "unanswered requests report reason=interaction_stalled");
}

void testExitCodeMapping()
{
    using R = NetworkUiSmokeReport;
    check(R::exitCodeForFailedStage(QStringLiteral("connected")) == R::ConnectFailed,
        "a failed connect maps to ConnectFailed");
    check(R::exitCodeForFailedStage(QStringLiteral("signed_up")) == R::SignupFailed,
        "a failed signup maps to SignupFailed");
    check(R::exitCodeForFailedStage(QStringLiteral("room_scene")) == R::RoomSceneFailed,
        "a failed RoomScene maps to RoomSceneFailed");
    check(R::exitCodeForFailedStage(QStringLiteral("dashboard")) == R::DashboardFailed,
        "a failed Dashboard maps to DashboardFailed");
    check(R::exitCodeForFailedStage(QStringLiteral("general_selected"))
            == R::GeneralSelectionFailed,
        "a failed general selection maps to GeneralSelectionFailed");
    check(R::exitCodeForFailedStage(QStringLiteral("game_started")) == R::GameStartFailed,
        "a game that never starts maps to GameStartFailed");
    check(R::exitCodeForFailedStage(QStringLiteral("game_over")) == R::GameOverNotReached,
        "a game that never ends maps to GameOverNotReached");
    check(R::exitCodeForFailedStage(QStringLiteral("arguments")) == R::InvalidArguments,
        "bad arguments map to InvalidArguments");
    check(R::exitCodeForFailedStage(QStringLiteral("nonsense")) == R::InternalError,
        "unknown stages fall back to InternalError");

    // 每一個失敗分類都要有自己的 exit code，唔可以撞埋一齊。
    const QList<int> codes{R::Passed, R::InvalidArguments, R::ConnectFailed, R::SignupFailed,
        R::RoomSceneFailed, R::DashboardFailed, R::GeneralSelectionFailed, R::GameStartFailed,
        R::InteractionFailed, R::GameOverNotReached, R::Disconnected, R::Timeout,
        R::InternalError};
    QSet<int> unique(codes.cbegin(), codes.cend());
    check(unique.size() == codes.size(), "every failure class has a distinct exit code");
    check(R::Passed == 0, "success is exit code 0");
}

void testFlagParsing()
{
    using R = NetworkUiSmokeReport;
    check(R::isRequested(QStringList{QStringLiteral("--network-ui-smoke")}),
        "the exact flag enables the smoke");
    // 前綴誤判會令 "--network-ui-smoke-result" 單獨出現時就啟動 smoke。
    check(!R::isRequested(QStringList{QStringLiteral("--network-ui-smoke-result"),
            QStringLiteral("out.json")}),
        "a longer flag does not enable the smoke by prefix");
    check(!R::isRequested(QStringList{QStringLiteral("-connect:127.0.0.1")}),
        "an ordinary launch does not enable the smoke");

    int value = 0;
    QString error;
    check(R::parseTimeoutMs(QStringList{QStringLiteral("--network-ui-smoke")}, &value, &error)
            && value == R::defaultTimeoutMs(),
        "an absent timeout falls back to the default");
    check(R::parseTimeoutMs(QStringList{QStringLiteral("--network-ui-smoke-timeout-ms"),
            QStringLiteral("120000")}, &value, &error) && value == 120000,
        "a separate-argument timeout parses");
    check(R::parseTimeoutMs(QStringList{QStringLiteral("--network-ui-smoke-timeout-ms=90000")},
            &value, &error) && value == 90000,
        "an inline timeout parses");
    check(!R::parseTimeoutMs(QStringList{QStringLiteral("--network-ui-smoke-timeout-ms"),
            QStringLiteral("abc")}, &value, &error) && !error.isEmpty(),
        "a non-numeric timeout is rejected with a message");
    check(!R::parseTimeoutMs(QStringList{QStringLiteral("--network-ui-smoke-timeout-ms"),
            QStringLiteral("0")}, &value, &error),
        "a timeout below the lower bound is rejected");
    check(!R::parseStallMs(QStringList{QStringLiteral("--network-ui-smoke-stall-ms"),
            QStringLiteral("-1")}, &value, &error),
        "a negative stall budget is rejected");

    check(R::parseResultPath(QStringList{QStringLiteral("--network-ui-smoke-result"),
            QStringLiteral("out.json")}) == QLatin1String("out.json"),
        "the result path parses");
    check(R::parseScreenshotPath(QStringList{
            QStringLiteral("--network-ui-smoke-screenshot=shot.png")})
            == QLatin1String("shot.png"),
        "the screenshot path parses inline");
    check(R::parseResultPath(QStringList{QStringLiteral("--network-ui-smoke")}).isEmpty(),
        "an absent result path stays empty");
}

void testInteractionNames()
{
    using namespace QSanProtocol;
    using R = NetworkUiSmokeReport;

    // M2 驗收清單直接對應呢啲名；改名等於改 CI gate。
    check(R::interactionName(S_COMMAND_RESPONSE_CARD) == QLatin1String("ask_for_card"),
        "response-card requests are ask_for_card");
    check(R::interactionName(S_COMMAND_ASK_PEACH) == QLatin1String("ask_for_card"),
        "peach requests are ask_for_card");
    check(R::interactionName(S_COMMAND_NULLIFICATION) == QLatin1String("ask_for_card"),
        "nullification requests are ask_for_card");
    check(R::interactionName(S_COMMAND_MULTIPLE_CHOICE) == QLatin1String("ask_for_choice"),
        "multiple-choice requests are ask_for_choice");
    check(R::interactionName(S_COMMAND_CHOOSE_PLAYER)
            == QLatin1String("ask_for_player_chosen"),
        "choose-player requests are ask_for_player_chosen");
    check(R::interactionName(S_COMMAND_INVOKE_SKILL)
            == QLatin1String("ask_for_skill_invoke"),
        "skill-invoke requests are ask_for_skill_invoke");
    check(R::interactionName(S_COMMAND_CHOOSE_GENERAL) == QLatin1String("choose_general"),
        "general-selection requests are choose_general");
    check(R::interactionName(S_COMMAND_PLAY_CARD) == QLatin1String("play_phase"),
        "play-phase requests are play_phase");
    check(R::interactionName(S_COMMAND_DISCARD_CARD) == QLatin1String("ask_for_discard"),
        "discard requests are ask_for_discard");

    // Notification／reply 唔算互動，唔應該污染覆蓋率統計。
    check(R::interactionName(S_COMMAND_GAME_START).isEmpty(),
        "notifications are not interactions");
    check(R::interactionName(S_COMMAND_SETUP).isEmpty(),
        "setup is not an interaction");
    check(R::interactionName(S_COMMAND_UNKNOWN).isEmpty(),
        "unknown commands are not interactions");

    const QStringList known = R::knownInteractionNames();
    for (const char *required : {"ask_for_card", "ask_for_choice", "ask_for_player_chosen",
             "ask_for_skill_invoke", "choose_general", "play_phase"}) {
        check(known.contains(QLatin1String(required)),
            "the acceptance interaction is part of the contract");
    }

    const QStringList actions = R::knownActionNames();
    for (const char *required : {"play_card", "select_target", "finish_phase",
             "choose_option", "choose_player", "invoke_skill", "trustee_fallback"}) {
        check(actions.contains(QLatin1String(required)),
            "the acceptance UI action is part of the contract");
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);

    testMarkerNames();
    testStageOrder();
    testStageLineSchema();
    testResultLineSchema();
    testExitCodeMapping();
    testFlagParsing();
    testInteractionNames();

    if (failures > 0) {
        printf("\n%d network UI smoke contract check(s) failed\n", failures);
        return 1;
    }
    printf("\nnetwork UI smoke contract OK\n");
    return 0;
}

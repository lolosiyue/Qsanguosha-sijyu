// Linux GUI M1 startup smoke 的 marker／exit code 契約測試。
//
// 只依賴 Qt Core：契約本身唔應該要開 QApplication 先驗到，CI 亦要喺 server-only
// runner 上照跑。真正的 GUI 啟動由 tools/ci/linux-gui-startup-smoke.sh 喺 Xvfb 下
// 驗證。
#include "ui-startup-smoke-report.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>

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
    check(QLatin1String(UiStartupSmokeReport::StageMarker) == QLatin1String("UI_STARTUP_STAGE"),
        "stage marker token is stable");
    check(QLatin1String(UiStartupSmokeReport::ResultMarker) == QLatin1String("UI_STARTUP_RESULT"),
        "result marker token is stable");
    check(QLatin1String(UiStartupSmokeReport::FlagStartupSmoke)
            == QLatin1String("--ui-startup-smoke"),
        "startup smoke CLI flag is stable");
    check(QLatin1String(UiStartupSmokeReport::FlagTimeoutMs)
            == QLatin1String("--ui-startup-timeout-ms"),
        "startup smoke timeout CLI flag is stable");
    check(UiStartupSmokeReport::schemaVersion() == 1, "result schema version is 1");
}

void testStageOrder()
{
    const QStringList stages = UiStartupSmokeReport::stageOrder();
    const QStringList expected{
        QStringLiteral("application"), QStringLiteral("engine"),
        QStringLiteral("main_window"), QStringLiteral("event_loop"),
        QStringLiteral("home_scene"), QStringLiteral("shutdown")
    };
    check(stages == expected, "stage order matches the documented startup path");
    check(UiStartupSmokeReport::isKnownStage(QStringLiteral("home_scene")),
        "home_scene is a known stage");
    check(!UiStartupSmokeReport::isKnownStage(QStringLiteral("nonsense")),
        "unknown stage names are rejected");
}

void testStageLineSchema()
{
    const QString line = UiStartupSmokeReport::stageLine(QStringLiteral("main_window"), true,
        QJsonObject{{QStringLiteral("width"), 1366}});
    const QJsonObject payload =
        parseMarker(line, QLatin1String(UiStartupSmokeReport::StageMarker));
    check(!payload.isEmpty(), "stage line is one marker token plus compact JSON");
    check(payload.value(QStringLiteral("schema_version")).toInt() == 1,
        "stage payload carries the schema version");
    check(payload.value(QStringLiteral("stage")).toString() == QLatin1String("main_window"),
        "stage payload names the stage");
    check(payload.value(QStringLiteral("ok")).toBool(), "stage payload carries ok");
    check(payload.value(QStringLiteral("width")).toInt() == 1366,
        "stage payload keeps caller-supplied details");
    check(!line.contains(QLatin1Char('\n')), "stage line is a single line");
}

void testSuccessResultContract()
{
    const QString line = UiStartupSmokeReport::resultLine(true, QStringLiteral("shutdown"),
        QString(), UiStartupSmokeReport::Passed);
    const QJsonObject payload =
        parseMarker(line, QLatin1String(UiStartupSmokeReport::ResultMarker));
    check(!payload.isEmpty(), "result line is one marker token plus compact JSON");
    check(payload.value(QStringLiteral("ok")).toBool(), "success result reports ok=true");
    check(payload.value(QStringLiteral("exit_code")).toInt() == 0,
        "success result maps to exit code 0");
    check(payload.value(QStringLiteral("reason")).toString() == QLatin1String("ok"),
        "success result reason is ok");
    check(!payload.contains(QStringLiteral("error")),
        "success result carries no error field");
}

void testFailureResultContract()
{
    const QString line = UiStartupSmokeReport::resultLine(false, QStringLiteral("home_scene"),
        QStringLiteral("module \"QtQuick\" is not installed"),
        UiStartupSmokeReport::QmlLoadFailed);
    const QJsonObject payload =
        parseMarker(line, QLatin1String(UiStartupSmokeReport::ResultMarker));
    check(!payload.value(QStringLiteral("ok")).toBool(), "failure result reports ok=false");
    check(payload.value(QStringLiteral("stage")).toString() == QLatin1String("home_scene"),
        "failure result names the failing stage");
    check(payload.value(QStringLiteral("exit_code")).toInt()
            == static_cast<int>(UiStartupSmokeReport::QmlLoadFailed),
        "QML load failure maps to a non-zero exit code");
    check(payload.value(QStringLiteral("exit_code")).toInt() != 0,
        "failure exit code is never 0");
    check(payload.value(QStringLiteral("reason")).toString() == QLatin1String("stage_failed"),
        "non-timeout failure reason is stage_failed");
    check(payload.value(QStringLiteral("error")).toString().contains(
            QLatin1String("is not installed")),
        "failure result preserves the QML error text");
}

void testTimeoutResultContract()
{
    const QString line = UiStartupSmokeReport::resultLine(false, QStringLiteral("home_scene"),
        QStringLiteral("startup smoke timed out"), UiStartupSmokeReport::Timeout);
    const QJsonObject payload =
        parseMarker(line, QLatin1String(UiStartupSmokeReport::ResultMarker));
    check(payload.value(QStringLiteral("reason")).toString() == QLatin1String("timeout"),
        "timeout is distinguishable from a plain stage failure");
    check(payload.value(QStringLiteral("exit_code")).toInt()
            == static_cast<int>(UiStartupSmokeReport::Timeout),
        "timeout maps to the timeout exit code");
    check(static_cast<int>(UiStartupSmokeReport::Timeout) != 0,
        "timeout exit code is non-zero");
}

void testExitCodeMapping()
{
    check(UiStartupSmokeReport::exitCodeForFailedStage(QStringLiteral("application"))
            == UiStartupSmokeReport::SetupFailed, "application failure is a setup failure");
    check(UiStartupSmokeReport::exitCodeForFailedStage(QStringLiteral("engine"))
            == UiStartupSmokeReport::SetupFailed, "engine failure is a setup failure");
    check(UiStartupSmokeReport::exitCodeForFailedStage(QStringLiteral("main_window"))
            == UiStartupSmokeReport::SetupFailed, "main window failure is a setup failure");
    check(UiStartupSmokeReport::exitCodeForFailedStage(QStringLiteral("home_scene"))
            == UiStartupSmokeReport::QmlLoadFailed, "home scene failure is a QML load failure");
    check(UiStartupSmokeReport::exitCodeForFailedStage(QStringLiteral("who-knows"))
            == UiStartupSmokeReport::InternalError, "unknown stages map to InternalError");
    check(static_cast<int>(UiStartupSmokeReport::Passed) == 0, "Passed is exit code 0");
}

void testArgumentParsing()
{
    check(UiStartupSmokeReport::isRequested({QStringLiteral("--ui-startup-smoke")}),
        "the exact flag requests the startup smoke");
    check(!UiStartupSmokeReport::isRequested({QStringLiteral("--ui-startup-smoke-extra")}),
        "a longer flag is not mistaken for the startup smoke");
    check(!UiStartupSmokeReport::isRequested({QStringLiteral("--local-response-ui-capabilities")}),
        "the M0 capability query does not request the startup smoke");

    int timeoutMs = 0;
    QString error;
    check(UiStartupSmokeReport::parseTimeoutMs({}, &timeoutMs, &error)
            && timeoutMs == UiStartupSmokeReport::defaultTimeoutMs(),
        "the timeout defaults when the flag is absent");
    check(UiStartupSmokeReport::parseTimeoutMs(
            {QStringLiteral("--ui-startup-timeout-ms"), QStringLiteral("9000")},
            &timeoutMs, &error) && timeoutMs == 9000,
        "the timeout accepts a separate value");
    check(UiStartupSmokeReport::parseTimeoutMs(
            {QStringLiteral("--ui-startup-timeout-ms=9500")}, &timeoutMs, &error)
            && timeoutMs == 9500,
        "the timeout accepts an inline value");
    check(!UiStartupSmokeReport::parseTimeoutMs(
            {QStringLiteral("--ui-startup-timeout-ms"), QStringLiteral("nope")},
            &timeoutMs, &error) && !error.isEmpty(),
        "a non-integer timeout is rejected with an explanation");
    check(!UiStartupSmokeReport::parseTimeoutMs(
            {QStringLiteral("--ui-startup-timeout-ms"), QStringLiteral("0")},
            &timeoutMs, &error),
        "a timeout below the floor is rejected");
    check(!UiStartupSmokeReport::parseTimeoutMs(
            {QStringLiteral("--ui-startup-timeout-ms"), QStringLiteral("999999")},
            &timeoutMs, &error),
        "a timeout above the ceiling is rejected");
    check(UiStartupSmokeReport::parseReportPath(
            {QStringLiteral("--ui-startup-report"), QStringLiteral("/tmp/r.json")})
            == QLatin1String("/tmp/r.json"),
        "the report path is parsed");
}

void testOptionalAssetClassification()
{
    // Clean checkout 冇入庫 optional 美術資源，呢啲 warning 唔可以當成 QML 載入失敗。
    check(UiStartupSmokeReport::isOptionalAssetWarning(
            QStringLiteral("qrc:/QSanguosha/Home/HomeNavButton.qml:187:9: QML Image: "
                           "Cannot open: qrc:/QSanguosha/Home/icons/home.svg")),
        "a missing icon is an optional asset warning");
    check(UiStartupSmokeReport::isOptionalAssetWarning(
            QStringLiteral("QML Image: Cannot open: "
                           "file:///src/image/system/backdrop/new-version.jpg")),
        "a missing backdrop is an optional asset warning");
    check(UiStartupSmokeReport::isOptionalAssetWarning(
            QStringLiteral("Cannot open: audio/system/BGM/front-bgm.ogg")),
        "a missing audio clip is an optional asset warning");

    // 真正的 QML component 失敗唔可以被降級成 warning。
    check(!UiStartupSmokeReport::isOptionalAssetWarning(
            QStringLiteral("qrc:/QSanguosha/Home/HomeScene.qml:2:1: "
                           "module \"QtQuick.Controls\" is not installed")),
        "a missing QML module is not an optional asset warning");
    check(!UiStartupSmokeReport::isOptionalAssetWarning(
            QStringLiteral("qrc:/QSanguosha/Home/HomeScene.qml:8:1: "
                           "Type HomeBackground unavailable")),
        "an unavailable QML type is not an optional asset warning");
    check(!UiStartupSmokeReport::isOptionalAssetWarning(
            QStringLiteral("This application failed to start because no Qt platform "
                           "plugin could be initialized")),
        "a platform plugin failure is not an optional asset warning");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    testMarkerNames();
    testStageOrder();
    testStageLineSchema();
    testSuccessResultContract();
    testFailureResultContract();
    testTimeoutResultContract();
    testExitCodeMapping();
    testArgumentParsing();
    testOptionalAssetClassification();
    if (failures > 0) {
        printf("\n%d check(s) failed\n", failures);
        return 1;
    }
    printf("\nall UI startup smoke contract checks passed\n");
    return 0;
}

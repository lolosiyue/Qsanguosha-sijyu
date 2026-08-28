// Linux GUI M2B-A multimedia smoke 的 marker／exit code／影片分類契約測試。
//
// 只依賴 Qt Core：契約唔應該要開 QApplication、Qt Multimedia 或者真音訊裝置先
// 驗到。真正的 audio backend／QML media component 由
// tools/ci/linux-gui-multimedia-smoke.sh 喺 Xvfb 下驗證。
#include "multimedia-smoke-report.h"

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
    check(QLatin1String(MultimediaSmokeReport::StageMarker)
            == QLatin1String("MULTIMEDIA_STAGE"),
        "stage marker token is stable");
    check(QLatin1String(MultimediaSmokeReport::ResultMarker)
            == QLatin1String("MULTIMEDIA_RESULT"),
        "result marker token is stable");
    check(QLatin1String(MultimediaSmokeReport::VideoMarker)
            == QLatin1String("VIDEO_BACKEND_RESULT"),
        "video marker token is stable");
    check(QLatin1String(MultimediaSmokeReport::FlagMultimediaSmoke)
            == QLatin1String("--multimedia-smoke"),
        "multimedia smoke CLI flag is stable");
    check(QLatin1String(MultimediaSmokeReport::FlagReportPath)
            == QLatin1String("--multimedia-report"),
        "multimedia report CLI flag is stable");
    check(QLatin1String(MultimediaSmokeReport::FlagTimeoutMs)
            == QLatin1String("--multimedia-timeout-ms"),
        "multimedia timeout CLI flag is stable");
    check(MultimediaSmokeReport::schemaVersion() == 1, "result schema version is 1");
}

void testStageOrder()
{
    const QStringList stages = MultimediaSmokeReport::stageOrder();
    const QStringList expected{
        QStringLiteral("backend"), QStringLiteral("ui_effect"), QStringLiteral("voice"),
        QStringLiteral("bgm"), QStringLiteral("missing_asset"), QStringLiteral("video"),
        QStringLiteral("shutdown")
    };
    check(stages == expected, "stage order matches the documented contract");
    check(MultimediaSmokeReport::isKnownStage(QStringLiteral("voice")),
        "voice is a known stage");
    check(!MultimediaSmokeReport::isKnownStage(QStringLiteral("spine")),
        "an unrelated stage name is rejected");
}

void testStageLineSchema()
{
    const QString line = MultimediaSmokeReport::stageLine(QStringLiteral("bgm"), true,
        QJsonObject{{QStringLiteral("fixture_available"), true}});
    const QJsonObject payload =
        parseMarker(line, QLatin1String(MultimediaSmokeReport::StageMarker));
    check(!payload.isEmpty(), "stage line is a parseable marker");
    check(payload.value(QStringLiteral("stage")).toString() == QLatin1String("bgm"),
        "stage line carries the stage name");
    check(payload.value(QStringLiteral("ok")).toBool(), "stage line carries the ok flag");
    check(payload.value(QStringLiteral("schema_version")).toInt() == 1,
        "stage line carries the schema version");
    check(payload.value(QStringLiteral("fixture_available")).toBool(),
        "stage line keeps caller-supplied details");
}

void testSuccessResultContract()
{
    const QString line = MultimediaSmokeReport::resultLine(true, QStringLiteral("shutdown"),
        QString(), MultimediaSmokeReport::Passed);
    const QJsonObject payload =
        parseMarker(line, QLatin1String(MultimediaSmokeReport::ResultMarker));
    check(payload.value(QStringLiteral("ok")).toBool(), "a passing result reports ok");
    check(payload.value(QStringLiteral("exit_code")).toInt() == 0,
        "a passing result reports exit code 0");
    check(payload.value(QStringLiteral("reason")).toString() == QLatin1String("ok"),
        "a passing result reports reason ok");
    check(!payload.contains(QStringLiteral("error")),
        "a passing result carries no error field");
}

void testFailureResultContract()
{
    const QString line = MultimediaSmokeReport::resultLine(false, QStringLiteral("voice"),
        QStringLiteral("pool exhausted"), MultimediaSmokeReport::AudioStageFailed);
    const QJsonObject payload =
        parseMarker(line, QLatin1String(MultimediaSmokeReport::ResultMarker));
    check(!payload.value(QStringLiteral("ok")).toBool(), "a failing result reports not ok");
    check(payload.value(QStringLiteral("stage")).toString() == QLatin1String("voice"),
        "a failing result names the stage");
    check(payload.value(QStringLiteral("reason")).toString()
            == QLatin1String("stage_failed"),
        "a stage failure is distinguishable from a timeout");
    check(payload.value(QStringLiteral("error")).toString()
            == QLatin1String("pool exhausted"),
        "a failing result carries the error text");
}

void testTimeoutResultContract()
{
    const QString line = MultimediaSmokeReport::resultLine(false, QStringLiteral("bgm"),
        QStringLiteral("timed out"), MultimediaSmokeReport::Timeout);
    const QJsonObject payload =
        parseMarker(line, QLatin1String(MultimediaSmokeReport::ResultMarker));
    check(payload.value(QStringLiteral("reason")).toString() == QLatin1String("timeout"),
        "a timeout reports reason timeout even on an audio stage");
    check(payload.value(QStringLiteral("exit_code")).toInt()
            == MultimediaSmokeReport::Timeout,
        "a timeout keeps the timeout exit code");
}

void testVideoContract()
{
    const QJsonObject video = MultimediaSmokeReport::videoPayload(true, false, true,
        QLatin1String(MultimediaSmokeReport::VideoCodecUnsupported),
        QStringLiteral("InvalidMedia"));
    const QJsonObject payload = parseMarker(MultimediaSmokeReport::videoLine(video),
        QLatin1String(MultimediaSmokeReport::VideoMarker));
    check(payload.value(QStringLiteral("schema_version")).toInt() == 1,
        "video result carries the schema version");
    check(payload.value(QStringLiteral("available")).toBool(),
        "video result reports backend availability");
    check(!payload.value(QStringLiteral("loaded")).toBool(),
        "video result reports that nothing loaded");
    check(payload.value(QStringLiteral("fallback")).toBool(),
        "video result reports that the static fallback took over");
    check(payload.value(QStringLiteral("reason")).toString()
            == QLatin1String("codec_unsupported"),
        "video result keeps the failure classification");

    // 「冇 console error」唔可以當成功：每一種失敗都要有自己的分類。
    for (const QString &reason : {QStringLiteral("asset_missing"),
             QStringLiteral("backend_unavailable"), QStringLiteral("codec_unsupported"),
             QStringLiteral("playback_error"), QStringLiteral("fallback_ok"),
             QStringLiteral("disabled"), QStringLiteral("not_requested"),
             QStringLiteral("ok")}) {
        check(MultimediaSmokeReport::isKnownVideoReason(reason),
            qPrintable(QStringLiteral("video reason '%1' is classified").arg(reason)));
    }
    check(!MultimediaSmokeReport::isKnownVideoReason(QStringLiteral("something_else")),
        "an unclassified video reason is rejected");
    check(!MultimediaSmokeReport::isAcceptableVideoReason(QString()),
        "an empty video reason is never acceptable");
}

void testExitCodeMapping()
{
    check(MultimediaSmokeReport::exitCodeForFailedStage(QStringLiteral("backend"))
            == MultimediaSmokeReport::AudioStageFailed,
        "backend maps to the audio stage exit code");
    check(MultimediaSmokeReport::exitCodeForFailedStage(QStringLiteral("bgm"))
            == MultimediaSmokeReport::AudioStageFailed,
        "bgm maps to the audio stage exit code");
    check(MultimediaSmokeReport::exitCodeForFailedStage(QStringLiteral("video"))
            == MultimediaSmokeReport::VideoStageFailed,
        "video maps to its own exit code");
    check(MultimediaSmokeReport::exitCodeForFailedStage(QStringLiteral("main_window"))
            == MultimediaSmokeReport::SetupFailed,
        "GUI setup stages map to the setup exit code");
    check(MultimediaSmokeReport::exitCodeForFailedStage(QStringLiteral("arguments"))
            == MultimediaSmokeReport::InvalidArguments,
        "argument failures map to the invalid-arguments exit code");
    check(MultimediaSmokeReport::exitCodeForFailedStage(QStringLiteral("unknown"))
            == MultimediaSmokeReport::InternalError,
        "an unknown stage maps to the internal error exit code");
}

void testArgumentParsing()
{
    check(MultimediaSmokeReport::isRequested(QStringList{QStringLiteral("--multimedia-smoke")}),
        "the smoke flag is recognised");
    check(!MultimediaSmokeReport::isRequested(
            QStringList{QStringLiteral("--multimedia-smoke-foo")}),
        "a prefix of the smoke flag is not recognised");

    int timeout = 0;
    QString error;
    check(MultimediaSmokeReport::parseTimeoutMs(QStringList{}, &timeout, &error)
            && timeout == MultimediaSmokeReport::defaultTimeoutMs(),
        "the timeout defaults when the flag is absent");
    check(MultimediaSmokeReport::parseTimeoutMs(
              QStringList{QStringLiteral("--multimedia-timeout-ms"), QStringLiteral("5000")},
              &timeout, &error)
            && timeout == 5000,
        "a separated timeout value is parsed");
    check(MultimediaSmokeReport::parseTimeoutMs(
              QStringList{QStringLiteral("--multimedia-timeout-ms=7000")}, &timeout, &error)
            && timeout == 7000,
        "an inline timeout value is parsed");
    check(!MultimediaSmokeReport::parseTimeoutMs(
              QStringList{QStringLiteral("--multimedia-timeout-ms"), QStringLiteral("abc")},
              &timeout, &error)
            && !error.isEmpty(),
        "a non-integer timeout is rejected with an explanation");
    check(!MultimediaSmokeReport::parseTimeoutMs(
              QStringList{QStringLiteral("--multimedia-timeout-ms"), QStringLiteral("0")},
              &timeout, &error),
        "a timeout below the minimum is rejected");

    check(MultimediaSmokeReport::parseReportPath(
              QStringList{QStringLiteral("--multimedia-report"), QStringLiteral("out.json")})
            == QLatin1String("out.json"),
        "a separated report path is parsed");
    check(MultimediaSmokeReport::parseReportPath(
              QStringList{QStringLiteral("--multimedia-report=out.json")})
            == QLatin1String("out.json"),
        "an inline report path is parsed");
    check(MultimediaSmokeReport::parseReportPath(QStringList{}).isEmpty(),
        "no report path means no report file");
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
    testVideoContract();
    testExitCodeMapping();
    testArgumentParsing();
    if (failures > 0) {
        printf("\n%d check(s) failed\n", failures);
        return 1;
    }
    printf("\nall multimedia smoke contract checks passed\n");
    return 0;
}

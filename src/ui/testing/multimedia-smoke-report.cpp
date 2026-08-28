#include "multimedia-smoke-report.h"

#include <QJsonDocument>

namespace {

const int kSchemaVersion = 1;
const int kDefaultTimeoutMs = 30000;
// 下限只係擋 0／負數之類的手誤；負向契約測試需要一個一定會觸發的極短 timeout。
const int kMinimumTimeoutMs = 100;
const int kMaximumTimeoutMs = 180000;

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

QString compactJson(const QJsonObject &payload)
{
    return QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

} // namespace

const char *const MultimediaSmokeReport::StageMarker = "MULTIMEDIA_STAGE";
const char *const MultimediaSmokeReport::ResultMarker = "MULTIMEDIA_RESULT";
const char *const MultimediaSmokeReport::VideoMarker = "VIDEO_BACKEND_RESULT";
const char *const MultimediaSmokeReport::FlagMultimediaSmoke = "--multimedia-smoke";
const char *const MultimediaSmokeReport::FlagReportPath = "--multimedia-report";
const char *const MultimediaSmokeReport::FlagTimeoutMs = "--multimedia-timeout-ms";
const char *const MultimediaSmokeReport::FlagVideoSource = "--multimedia-video-source";
const char *const MultimediaSmokeReport::ReasonOk = "ok";
const char *const MultimediaSmokeReport::ReasonStageFailed = "stage_failed";
const char *const MultimediaSmokeReport::ReasonTimeout = "timeout";
const char *const MultimediaSmokeReport::VideoOk = "ok";
const char *const MultimediaSmokeReport::VideoNotRequested = "not_requested";
const char *const MultimediaSmokeReport::VideoDisabled = "disabled";
const char *const MultimediaSmokeReport::VideoAssetMissing = "asset_missing";
const char *const MultimediaSmokeReport::VideoBackendUnavailable = "backend_unavailable";
const char *const MultimediaSmokeReport::VideoCodecUnsupported = "codec_unsupported";
const char *const MultimediaSmokeReport::VideoPlaybackError = "playback_error";
const char *const MultimediaSmokeReport::VideoFallbackOk = "fallback_ok";

int MultimediaSmokeReport::schemaVersion()
{
    return kSchemaVersion;
}

int MultimediaSmokeReport::defaultTimeoutMs()
{
    return kDefaultTimeoutMs;
}

int MultimediaSmokeReport::minimumTimeoutMs()
{
    return kMinimumTimeoutMs;
}

int MultimediaSmokeReport::maximumTimeoutMs()
{
    return kMaximumTimeoutMs;
}

QStringList MultimediaSmokeReport::stageOrder()
{
    return QStringList{
        QStringLiteral("backend"),
        QStringLiteral("ui_effect"),
        QStringLiteral("voice"),
        QStringLiteral("bgm"),
        QStringLiteral("missing_asset"),
        QStringLiteral("video"),
        QStringLiteral("shutdown")
    };
}

bool MultimediaSmokeReport::isKnownStage(const QString &stage)
{
    return stageOrder().contains(stage);
}

QStringList MultimediaSmokeReport::videoReasons()
{
    return QStringList{
        QLatin1String(VideoOk),
        QLatin1String(VideoNotRequested),
        QLatin1String(VideoDisabled),
        QLatin1String(VideoAssetMissing),
        QLatin1String(VideoBackendUnavailable),
        QLatin1String(VideoCodecUnsupported),
        QLatin1String(VideoPlaybackError),
        QLatin1String(VideoFallbackOk)
    };
}

bool MultimediaSmokeReport::isKnownVideoReason(const QString &reason)
{
    return videoReasons().contains(reason);
}

bool MultimediaSmokeReport::isAcceptableVideoReason(const QString &reason)
{
    // 播得到、關咗、或者任何一種「播唔到但靜態背景頂得住」都算通過。
    // 唯一唔接受的係 reason 本身唔認識 —— 即係報告漏咗分類。
    return isKnownVideoReason(reason);
}

bool MultimediaSmokeReport::isRequested(const QStringList &arguments)
{
    return arguments.contains(QLatin1String(FlagMultimediaSmoke));
}

bool MultimediaSmokeReport::parseTimeoutMs(const QStringList &arguments, int *timeoutMs,
    QString *error)
{
    *timeoutMs = kDefaultTimeoutMs;
    if (error)
        error->clear();

    bool found = false;
    const QString raw = flagValue(arguments, QLatin1String(FlagTimeoutMs), &found);
    if (!found)
        return true;

    bool ok = false;
    const int value = raw.toInt(&ok);
    if (!ok) {
        if (error)
            *error = QStringLiteral("%1 expects an integer, got '%2'")
                .arg(QLatin1String(FlagTimeoutMs), raw);
        return false;
    }
    if (value < kMinimumTimeoutMs || value > kMaximumTimeoutMs) {
        if (error)
            *error = QStringLiteral("%1 must be within [%2, %3], got %4")
                .arg(QLatin1String(FlagTimeoutMs))
                .arg(kMinimumTimeoutMs)
                .arg(kMaximumTimeoutMs)
                .arg(value);
        return false;
    }
    *timeoutMs = value;
    return true;
}

QString MultimediaSmokeReport::parseReportPath(const QStringList &arguments)
{
    bool found = false;
    const QString value = flagValue(arguments, QLatin1String(FlagReportPath), &found);
    return found ? value : QString();
}

QString MultimediaSmokeReport::parseVideoSource(const QStringList &arguments)
{
    bool found = false;
    const QString value = flagValue(arguments, QLatin1String(FlagVideoSource), &found);
    return found ? value : QString();
}

QJsonObject MultimediaSmokeReport::stagePayload(const QString &stage, bool ok,
    const QJsonObject &details)
{
    QJsonObject payload = details;
    payload.insert(QStringLiteral("schema_version"), kSchemaVersion);
    payload.insert(QStringLiteral("stage"), stage);
    payload.insert(QStringLiteral("ok"), ok);
    return payload;
}

QJsonObject MultimediaSmokeReport::resultPayload(bool ok, const QString &stage,
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
        payload.insert(QStringLiteral("reason"),
            exitCode == Timeout ? QLatin1String(ReasonTimeout)
                                : QLatin1String(ReasonStageFailed));
        payload.insert(QStringLiteral("error"), error);
    }
    return payload;
}

QJsonObject MultimediaSmokeReport::videoPayload(bool available, bool loaded, bool fallback,
    const QString &reason, const QString &error)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("schema_version"), kSchemaVersion);
    payload.insert(QStringLiteral("available"), available);
    payload.insert(QStringLiteral("loaded"), loaded);
    payload.insert(QStringLiteral("fallback"), fallback);
    payload.insert(QStringLiteral("reason"), reason);
    payload.insert(QStringLiteral("error"), error);
    return payload;
}

QString MultimediaSmokeReport::stageLine(const QString &stage, bool ok,
    const QJsonObject &details)
{
    return QLatin1String(StageMarker) + QLatin1Char(' ')
        + compactJson(stagePayload(stage, ok, details));
}

QString MultimediaSmokeReport::resultLine(bool ok, const QString &stage,
    const QString &error, ExitCode exitCode, const QJsonObject &details)
{
    return QLatin1String(ResultMarker) + QLatin1Char(' ')
        + compactJson(resultPayload(ok, stage, error, exitCode, details));
}

QString MultimediaSmokeReport::videoLine(const QJsonObject &video)
{
    return QLatin1String(VideoMarker) + QLatin1Char(' ') + compactJson(video);
}

MultimediaSmokeReport::ExitCode MultimediaSmokeReport::exitCodeForFailedStage(
    const QString &stage)
{
    if (stage == QLatin1String("video"))
        return VideoStageFailed;
    if (stage == QLatin1String("backend") || stage == QLatin1String("ui_effect")
        || stage == QLatin1String("voice") || stage == QLatin1String("bgm")
        || stage == QLatin1String("missing_asset"))
        return AudioStageFailed;
    if (stage == QLatin1String("arguments"))
        return InvalidArguments;
    if (stage == QLatin1String("application") || stage == QLatin1String("engine")
        || stage == QLatin1String("main_window") || stage == QLatin1String("home_scene")
        || stage == QLatin1String("shutdown"))
        return SetupFailed;
    return InternalError;
}

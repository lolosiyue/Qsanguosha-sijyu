#include "ui-startup-smoke-report.h"

#include <QJsonDocument>
#include <QRegularExpression>

namespace {

const int kSchemaVersion = 1;
const int kDefaultTimeoutMs = 15000;
// 下限只係為咗擋 0／負數之類的手誤；負向契約測試需要一個一定會觸發的
// 極短 timeout，所以唔可以定得太高。實際使用建議 >= 15000。
const int kMinimumTimeoutMs = 100;
const int kMaximumTimeoutMs = 120000;

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

const char *const UiStartupSmokeReport::StageMarker = "UI_STARTUP_STAGE";
const char *const UiStartupSmokeReport::ResultMarker = "UI_STARTUP_RESULT";
const char *const UiStartupSmokeReport::FlagStartupSmoke = "--ui-startup-smoke";
const char *const UiStartupSmokeReport::FlagTimeoutMs = "--ui-startup-timeout-ms";
const char *const UiStartupSmokeReport::FlagReportPath = "--ui-startup-report";
const char *const UiStartupSmokeReport::ReasonOk = "ok";
const char *const UiStartupSmokeReport::ReasonStageFailed = "stage_failed";
const char *const UiStartupSmokeReport::ReasonTimeout = "timeout";

int UiStartupSmokeReport::schemaVersion()
{
    return kSchemaVersion;
}

int UiStartupSmokeReport::defaultTimeoutMs()
{
    return kDefaultTimeoutMs;
}

int UiStartupSmokeReport::minimumTimeoutMs()
{
    return kMinimumTimeoutMs;
}

int UiStartupSmokeReport::maximumTimeoutMs()
{
    return kMaximumTimeoutMs;
}

QStringList UiStartupSmokeReport::stageOrder()
{
    return QStringList{
        QStringLiteral("application"),
        QStringLiteral("engine"),
        QStringLiteral("main_window"),
        QStringLiteral("event_loop"),
        QStringLiteral("home_scene"),
        QStringLiteral("shutdown")
    };
}

bool UiStartupSmokeReport::isKnownStage(const QString &stage)
{
    return stageOrder().contains(stage);
}

bool UiStartupSmokeReport::isRequested(const QStringList &arguments)
{
    return arguments.contains(QLatin1String(FlagStartupSmoke));
}

bool UiStartupSmokeReport::parseTimeoutMs(const QStringList &arguments, int *timeoutMs,
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

QString UiStartupSmokeReport::parseReportPath(const QStringList &arguments)
{
    bool found = false;
    const QString value = flagValue(arguments, QLatin1String(FlagReportPath), &found);
    return found ? value : QString();
}

bool UiStartupSmokeReport::isOptionalAssetWarning(const QString &message)
{
    // 圖片／影片／音效載入失敗：clean checkout 冇入庫呢啲 optional 美術資源，
    // HomeScene 本身照樣載入成功，只係渲染時少咗貼圖。
    static const QRegularExpression assetPattern(
        QStringLiteral("(Cannot open|Error decoding|Failed to load|File not found|"
                       "Protocol \"\" is unknown|no such file or directory)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression suffixPattern(
        QStringLiteral("\\.(png|jpe?g|webp|gif|svg|bmp|ogg|wav|mp3|mp4|webm|atlas|skel|json)\\b"),
        QRegularExpression::CaseInsensitiveOption);

    if (!assetPattern.match(message).hasMatch())
        return false;
    return suffixPattern.match(message).hasMatch();
}

QJsonObject UiStartupSmokeReport::stagePayload(const QString &stage, bool ok,
    const QJsonObject &details)
{
    QJsonObject payload = details;
    payload.insert(QStringLiteral("schema_version"), kSchemaVersion);
    payload.insert(QStringLiteral("stage"), stage);
    payload.insert(QStringLiteral("ok"), ok);
    return payload;
}

QJsonObject UiStartupSmokeReport::resultPayload(bool ok, const QString &stage,
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

QString UiStartupSmokeReport::stageLine(const QString &stage, bool ok,
    const QJsonObject &details)
{
    return QLatin1String(StageMarker) + QLatin1Char(' ')
        + compactJson(stagePayload(stage, ok, details));
}

QString UiStartupSmokeReport::resultLine(bool ok, const QString &stage,
    const QString &error, ExitCode exitCode, const QJsonObject &details)
{
    return QLatin1String(ResultMarker) + QLatin1Char(' ')
        + compactJson(resultPayload(ok, stage, error, exitCode, details));
}

UiStartupSmokeReport::ExitCode UiStartupSmokeReport::exitCodeForFailedStage(const QString &stage)
{
    if (stage == QLatin1String("home_scene"))
        return QmlLoadFailed;
    if (stage == QLatin1String("arguments"))
        return InvalidArguments;
    if (stage == QLatin1String("application") || stage == QLatin1String("engine")
        || stage == QLatin1String("main_window") || stage == QLatin1String("event_loop")
        || stage == QLatin1String("shutdown"))
        return SetupFailed;
    return InternalError;
}

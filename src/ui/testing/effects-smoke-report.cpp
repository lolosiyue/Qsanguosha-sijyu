#include "effects-smoke-report.h"

#include <QJsonDocument>
#include <QJsonValue>

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

bool checkCounter(const QJsonObject &counters, const QString &key, int budget,
    QString *violation)
{
    if (budget < 0)
        return true;
    const int actual = counters.value(key).toInt(0);
    if (actual <= budget)
        return true;
    if (violation) {
        *violation = QStringLiteral("%1: created %2, budget is %3")
            .arg(key).arg(actual).arg(budget);
    }
    return false;
}

} // namespace

const char *const EffectsSmokeReport::StageMarker = "EFFECTS_STAGE";
const char *const EffectsSmokeReport::ResultMarker = "EFFECTS_RESULT";
const char *const EffectsSmokeReport::ProfileMarker = "EFFECTS_PROFILE_RESULT";

const char *const EffectsSmokeReport::StagePolicy = "policy";
const char *const EffectsSmokeReport::StageCompletion = "completion";
const char *const EffectsSmokeReport::StageAnimation = "animation";
const char *const EffectsSmokeReport::StageGif = "gif";
const char *const EffectsSmokeReport::StageSpine = "spine";
const char *const EffectsSmokeReport::StageBudget = "budget";
const char *const EffectsSmokeReport::StageShutdown = "shutdown";

const char *const EffectsSmokeReport::FlagEffectsSmoke = "--effects-smoke";
const char *const EffectsSmokeReport::FlagReportPath = "--effects-report";
const char *const EffectsSmokeReport::FlagTimeoutMs = "--effects-timeout-ms";
const char *const EffectsSmokeReport::FlagFixtureRoot = "--effects-fixtures";

const char *const EffectsSmokeReport::ReasonOk = "ok";
const char *const EffectsSmokeReport::ReasonStageFailed = "stage_failed";
const char *const EffectsSmokeReport::ReasonTimeout = "timeout";

int EffectsSmokeReport::schemaVersion()
{
    return kSchemaVersion;
}

int EffectsSmokeReport::defaultTimeoutMs()
{
    return kDefaultTimeoutMs;
}

int EffectsSmokeReport::minimumTimeoutMs()
{
    return kMinimumTimeoutMs;
}

int EffectsSmokeReport::maximumTimeoutMs()
{
    return kMaximumTimeoutMs;
}

QString EffectsSmokeReport::defaultFixtureRoot()
{
    return QStringLiteral("tests/fixtures/effects");
}

QStringList EffectsSmokeReport::stageOrder()
{
    return QStringList{
        QLatin1String(StagePolicy),
        QLatin1String(StageCompletion),
        QLatin1String(StageAnimation),
        QLatin1String(StageGif),
        QLatin1String(StageSpine),
        QLatin1String(StageBudget),
        QLatin1String(StageShutdown)
    };
}

bool EffectsSmokeReport::isKnownStage(const QString &stage)
{
    return stageOrder().contains(stage);
}

bool EffectsSmokeReport::isRequested(const QStringList &arguments)
{
    return arguments.contains(QLatin1String(FlagEffectsSmoke));
}

bool EffectsSmokeReport::parseTimeoutMs(const QStringList &arguments, int *timeoutMs,
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

QString EffectsSmokeReport::parseReportPath(const QStringList &arguments)
{
    bool found = false;
    const QString value = flagValue(arguments, QLatin1String(FlagReportPath), &found);
    return found ? value : QString();
}

QString EffectsSmokeReport::parseFixtureRoot(const QStringList &arguments)
{
    bool found = false;
    const QString value = flagValue(arguments, QLatin1String(FlagFixtureRoot), &found);
    if (!found || value.isEmpty())
        return defaultFixtureRoot();
    return value;
}

QJsonObject EffectsSmokeReport::stagePayload(const QString &stage, bool ok,
    const QJsonObject &details)
{
    QJsonObject payload = details;
    payload.insert(QStringLiteral("schema_version"), kSchemaVersion);
    payload.insert(QStringLiteral("stage"), stage);
    payload.insert(QStringLiteral("ok"), ok);
    return payload;
}

QJsonObject EffectsSmokeReport::resultPayload(bool ok, const QString &stage,
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

QString EffectsSmokeReport::stageLine(const QString &stage, bool ok,
    const QJsonObject &details)
{
    return QLatin1String(StageMarker) + QLatin1Char(' ')
        + compactJson(stagePayload(stage, ok, details));
}

QString EffectsSmokeReport::resultLine(bool ok, const QString &stage,
    const QString &error, ExitCode exitCode, const QJsonObject &details)
{
    return QLatin1String(ResultMarker) + QLatin1Char(' ')
        + compactJson(resultPayload(ok, stage, error, exitCode, details));
}

QString EffectsSmokeReport::profileLine(const QJsonObject &profile)
{
    QJsonObject payload = profile;
    payload.insert(QStringLiteral("schema_version"), kSchemaVersion);
    return QLatin1String(ProfileMarker) + QLatin1Char(' ') + compactJson(payload);
}

EffectsSmokeReport::ExitCode EffectsSmokeReport::exitCodeForFailedStage(const QString &stage)
{
    if (stage == QLatin1String(StagePolicy))
        return PolicyStageFailed;
    if (stage == QLatin1String(StageCompletion))
        return CompletionStageFailed;
    if (stage == QLatin1String(StageAnimation) || stage == QLatin1String(StageGif)
        || stage == QLatin1String(StageSpine))
        return AssetStageFailed;
    if (stage == QLatin1String(StageBudget) || stage == QLatin1String(StageShutdown))
        return BudgetStageFailed;
    return InternalError;
}

EffectsSmokeReport::ObjectBudget EffectsSmokeReport::budgetFor(const QString &profileName)
{
    ObjectBudget budget;
    if (profileName == QLatin1String("none")) {
        // NONE 嘅硬性定義：一個高成本效果物件都唔准建立。
        budget.spineItems = 0;
        budget.movieObjects = 0;
        budget.qmlOverlays = 0;
        budget.videoObjects = 0;
        return budget;
    }
    if (profileName == QLatin1String("reduced")) {
        // REDUCED 保留 QMovie（只用首幀），但唔准有 Spine／QML 疊層／影片。
        budget.spineItems = 0;
        budget.qmlOverlays = 0;
        budget.videoObjects = 0;
        return budget;
    }
    return budget;  // full：冇上限
}

bool EffectsSmokeReport::withinBudget(const ObjectBudget &budget, const QJsonObject &counters,
    QString *violation)
{
    if (violation)
        violation->clear();
    return checkCounter(counters, QStringLiteral("spine_items"), budget.spineItems, violation)
        && checkCounter(counters, QStringLiteral("movie_objects"), budget.movieObjects, violation)
        && checkCounter(counters, QStringLiteral("qml_overlays"), budget.qmlOverlays, violation)
        && checkCounter(counters, QStringLiteral("video_objects"), budget.videoObjects, violation);
}

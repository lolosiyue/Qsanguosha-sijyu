#ifndef EFFECTS_SMOKE_REPORT_H
#define EFFECTS_SMOKE_REPORT_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

// The effects smoke contract for Linux GUI M2B-B.
//
// Like M1/M2/M2B-A, this header depends on Qt Core only: the marker schema,
// exit code mapping, stage names, and argument parsing can all be verified
// directly by CTest without launching QApplication, without OpenGL, and
// without any art assets. The part that actually drives the GUI lives in
// EffectsSmokeController.
//
// The pass criteria are not "pretty visuals" but:
//   - the resolved profile matches what the CLI and the settings claim
//   - every feature gate follows the profile contract
//   - completion is always exactly once (finished, skipped, destroyed midway, timeout alike)
//   - missing or broken assets always degrade to static UI, without crashing or hanging
//   - the NONE profile creates no Spine, QMovie, QML overlay, or video object at all
//   - REDUCED loads fewer expensive effects than FULL
//   - no effect QObject is left behind after teardown
class EffectsSmokeReport
{
public:
    static const char *const StageMarker;   // "EFFECTS_STAGE"
    static const char *const ResultMarker;  // "EFFECTS_RESULT"
    static const char *const ProfileMarker; // "EFFECTS_PROFILE"

    static int schemaVersion();

    enum ExitCode {
        Passed = 0,
        SetupFailed = 1,          // QApplication／engine／MainWindow 未能建立
        PolicyStageFailed = 2,    // profile resolution or feature gate mismatch
        CompletionStageFailed = 3,// exactly-once contract violated
        AssetStageFailed = 4,     // missing or broken asset failed to degrade (gif/spine/animation)
        BudgetStageFailed = 5,    // profile created objects it must not create
        Timeout = 6,              // app 內部 timeout
        InvalidArguments = 7,
        InternalError = 8
    };

    static QStringList stageOrder();
    static bool isKnownStage(const QString &stage);

    static const char *const StagePolicy;      // "policy"
    static const char *const StageCompletion;  // "completion"
    static const char *const StageAnimation;   // "animation"
    static const char *const StageGif;         // "gif"
    static const char *const StageSpine;       // "spine"
    static const char *const StageBudget;      // "budget"
    static const char *const StageShutdown;    // "shutdown"

    static const char *const FlagEffectsSmoke;    // "--effects-smoke"
    static const char *const FlagReportPath;      // "--effects-report"
    static const char *const FlagTimeoutMs;       // "--effects-timeout-ms"
    // Fixture root directory. Defaults to tests/fixtures/effects/; when it is
    // missing the stage does not fail, it only sets fixtures_available=false —
    // a missing fixture must be distinguishable from a genuinely broken one.
    static const char *const FlagFixtureRoot;     // "--effects-fixtures"

    static int defaultTimeoutMs();
    static int minimumTimeoutMs();
    static int maximumTimeoutMs();
    static QString defaultFixtureRoot();

    static bool isRequested(const QStringList &arguments);
    static bool parseTimeoutMs(const QStringList &arguments, int *timeoutMs, QString *error);
    static QString parseReportPath(const QStringList &arguments);
    static QString parseFixtureRoot(const QStringList &arguments);

    static const char *const ReasonOk;          // "ok"
    static const char *const ReasonStageFailed; // "stage_failed"
    static const char *const ReasonTimeout;     // "timeout"

    static QString stageLine(const QString &stage, bool ok,
        const QJsonObject &details = QJsonObject());
    static QString resultLine(bool ok, const QString &stage, const QString &error,
        ExitCode exitCode, const QJsonObject &details = QJsonObject());
    static QString profileLine(const QJsonObject &profile);

    static QJsonObject stagePayload(const QString &stage, bool ok,
        const QJsonObject &details = QJsonObject());
    static QJsonObject resultPayload(bool ok, const QString &stage, const QString &error,
        ExitCode exitCode, const QJsonObject &details = QJsonObject());

    static ExitCode exitCodeForFailedStage(const QString &stage);

    // ── Profile budget ──────────────────────────────────────────────────
    // How many expensive objects each profile may create. -1 = no cap. This
    // table is the executable definition of "NONE creates no Spine, QMovie,
    // or video object".
    struct ObjectBudget {
        int spineItems = -1;
        int movieObjects = -1;
        int qmlOverlays = -1;
        int videoObjects = -1;
    };
    static ObjectBudget budgetFor(const QString &profileName);
    static bool withinBudget(const ObjectBudget &budget, const QJsonObject &counters,
        QString *violation);
};

#endif

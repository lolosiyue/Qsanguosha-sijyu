#ifndef UI_STARTUP_SMOKE_REPORT_H
#define UI_STARTUP_SMOKE_REPORT_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

// The startup smoke contract for Linux GUI M1.
//
// This header depends on Qt Core only, with no Widgets/Quick/QML dependency, so
// CTest can verify the marker schema and exit code contract directly without
// launching QApplication. The part that actually drives the GUI lives in
// UiStartupSmokeController.
class UiStartupSmokeReport
{
public:
    // CI parses these stdout marker prefixes; changing them changes the report contract.
    static const char *const StageMarker;  // "UI_STARTUP_STAGE"
    static const char *const ResultMarker; // "UI_STARTUP_RESULT"

    // Increment the marker schema version when field semantics change incompatibly.
    static int schemaVersion();

    enum ExitCode {
        Passed = 0,
        SetupFailed = 1,   // QApplication, Engine or MainWindow could not be created.
        QmlLoadFailed = 2, // HomeScene or a QML component failed to load.
        Timeout = 3,       // Application-level timeout.
        InvalidArguments = 4,
        InternalError = 5
    };

    // Stage names in execution order.
    static QStringList stageOrder();
    static bool isKnownStage(const QString &stage);

    static const char *const FlagStartupSmoke; // "--ui-startup-smoke"
    static const char *const FlagTimeoutMs;    // "--ui-startup-timeout-ms"
    static const char *const FlagReportPath;   // "--ui-startup-report"
    static const char *const FlagStartupPage;

    static int defaultTimeoutMs();
    static int minimumTimeoutMs();
    static int maximumTimeoutMs();

    // Accept only the exact flag or --flag=value; do not treat a longer prefix as the same option.

    static bool isRequested(const QStringList &arguments);
    static bool parseTimeoutMs(const QStringList &arguments, int *timeoutMs, QString *error);
    static QString parseReportPath(const QStringList &arguments);
    static bool parseStartupPage(const QStringList &arguments, QString *page, QString *error);

    // Missing optional art assets (icons, character art, sounds) must not be
    // escalated to fatal: a clean checkout does not commit these files in the
    // first place. A genuine QML component load failure is judged by
    // QQuickWidget::Error, not by warning text.
    static bool isOptionalAssetWarning(const QString &message);

    // Failure reason of the result; timeout and "stage itself failed" share the
    // same stage name and are told apart by this field, so a timeout always maps
    // to the Timeout exit code.
    static const char *const ReasonOk;          // "ok"
    static const char *const ReasonStageFailed; // "stage_failed"
    static const char *const ReasonTimeout;     // "timeout"

    static QString stageLine(const QString &stage, bool ok,
        const QJsonObject &details = QJsonObject());
    static QString resultLine(bool ok, const QString &stage, const QString &error,
        ExitCode exitCode, const QJsonObject &details = QJsonObject());

    static QJsonObject stagePayload(const QString &stage, bool ok,
        const QJsonObject &details = QJsonObject());
    static QJsonObject resultPayload(bool ok, const QString &stage, const QString &error,
        ExitCode exitCode, const QJsonObject &details = QJsonObject());

    // Fixed mapping from failed stage to exit code; the caller handles timeout.
    static ExitCode exitCodeForFailedStage(const QString &stage);
};

#endif

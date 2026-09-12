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
    // stdout marker 前綴。CI 靠呢兩個 token 解析結果，改動即係改契約。
    static const char *const StageMarker;  // "UI_STARTUP_STAGE"
    static const char *const ResultMarker; // "UI_STARTUP_RESULT"

    // marker payload 的 schema version；欄位語意有 breaking change 先加。
    static int schemaVersion();

    enum ExitCode {
        Passed = 0,
        SetupFailed = 1,   // QApplication／engine／MainWindow 未能建立
        QmlLoadFailed = 2, // HomeScene／QML component 載入失敗
        Timeout = 3,       // app 內部 timeout 觸發
        InvalidArguments = 4,
        InternalError = 5
    };

    // stage 名稱，按實際發生次序。
    static QStringList stageOrder();
    static bool isKnownStage(const QString &stage);

    static const char *const FlagStartupSmoke; // "--ui-startup-smoke"
    static const char *const FlagTimeoutMs;    // "--ui-startup-timeout-ms"
    static const char *const FlagReportPath;   // "--ui-startup-report"
    static const char *const FlagStartupPage;

    static int defaultTimeoutMs();
    static int minimumTimeoutMs();
    static int maximumTimeoutMs();

    // 只認完全相符的 flag 或者 "--flag=value" 形式，避免 "--ui-startup-smoke-foo"
    // 之類的前綴誤判。
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

    // 失敗 stage → exit code 的固定對照（timeout 除外，由 caller 直接傳 Timeout）。
    static ExitCode exitCodeForFailedStage(const QString &stage);
};

#endif

#ifndef UI_STARTUP_SMOKE_REPORT_H
#define UI_STARTUP_SMOKE_REPORT_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

// Linux GUI M1 的 startup smoke 契約。
//
// 呢個 header 只依賴 Qt Core，冇任何 Widgets／Quick／QML 依賴，方便 CTest 直接
// 驗證 marker schema 同 exit code 契約，唔使開 QApplication。實際驅動 GUI 的部分
// 喺 UiStartupSmokeController。
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

    // 缺少 optional 美術資源（icon／立繪／音效）唔應該升級成 fatal：clean checkout
    // 本身就冇入庫呢啲檔案。真正的 QML component load 失敗由 QQuickWidget::Error
    // 判定，唔靠 warning 文字。
    static bool isOptionalAssetWarning(const QString &message);

    // result 的失敗原因；timeout 同「stage 本身失敗」用同一個 stage 名，靠呢個欄位
    // 分辨，所以 timeout 一定會 map 去 Timeout exit code。
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

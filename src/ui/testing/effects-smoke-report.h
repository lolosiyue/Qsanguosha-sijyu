#ifndef EFFECTS_SMOKE_REPORT_H
#define EFFECTS_SMOKE_REPORT_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

// Linux GUI M2B-B 的 effects smoke 契約。
//
// 同 M1／M2／M2B-A 一樣，呢個 header 只依賴 Qt Core：marker schema、exit code
// 對照、stage 名同參數解析都可以喺 CTest 直接驗，唔使開 QApplication、唔使有
// OpenGL、亦唔使有任何美術資產。真正驅動 GUI 的部分喺 EffectsSmokeController。
//
// 通過條件唔係「畫面靚」，而係：
//   - 解析出嚟嘅 profile 同 CLI／設定講嘅一樣
//   - 每個 feature gate 都跟 profile 契約
//   - completion 一定係 exactly once（播完、跳過、中途銷毀、timeout 都一樣）
//   - 缺／壞資產一律降級成靜態 UI，唔 crash、唔 hang
//   - NONE profile 一個 Spine／QMovie／QML overlay／video object 都冇建立
//   - REDUCED 載入嘅高成本效果比 FULL 少
//   - 收檔之後冇殘留嘅效果 QObject
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
        PolicyStageFailed = 2,    // profile 解析或者 feature gate 唔對
        CompletionStageFailed = 3,// exactly-once 契約破咗
        AssetStageFailed = 4,     // 缺／壞資產冇降級（gif／spine／animation）
        BudgetStageFailed = 5,    // profile 唔應該建立嘅物件建立咗
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
    // fixture 根目錄。預設 tests/fixtures/effects/；缺失時 stage 唔會失敗，
    // 只會標記 fixtures_available=false —— 缺 fixture 同真係壞咗要分得開。
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
    // 每個 profile 准許建立幾多個高成本物件。-1 = 冇上限。呢個表就係
    // 「NONE 唔建立 Spine／QMovie／video object」嘅可執行定義。
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

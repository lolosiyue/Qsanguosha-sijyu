#ifndef UI_STARTUP_SMOKE_CONTROLLER_H
#define UI_STARTUP_SMOKE_CONTROLLER_H

#include "ui-startup-smoke-report.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QStringList>

class MainWindow;
class QTimer;

// Linux GUI M1 的真正 startup smoke。
//
// 同 --local-response-ui-capabilities（喺 QApplication 之前就 return 的 binary
// capability query）唔同，呢個 controller 一定行完整 GUI startup path：
//
//     QApplication → engine/runtime → MainWindow → HomeScene/QML → event loop
//
// 佢唔會另外複製一份假的 HomeScene 啟動流程，而係直接建立產品用的 MainWindow，
// 靠 MainWindow 自己的 home scene signal 判斷 ready，再自動退出。
class UiStartupSmokeController final : public QObject
{
    Q_OBJECT

public:
    explicit UiStartupSmokeController(QObject *parent = nullptr);
    ~UiStartupSmokeController() override;

    static bool isRequested(const QStringList &arguments);

    // 喺 QApplication 建立之後、engine bootstrap 之前呼叫：安裝 Qt message hook，
    // 並記錄 application stage。回傳 false 代表參數不合法（已輸出 failure result）。
    static bool begin(const QStringList &arguments, int *exitCode);

    // GUI 初始化中途失敗（例如 EngineBootstrap）時呼叫：輸出 failure result 並
    // 回傳對應 exit code。冇 begin() 過就當冇要求 smoke，回傳 fallback。
    static int abortEarly(const QString &stage, const QString &error, int fallbackExitCode);

    // 走完產品正常 GUI 初始化之後呼叫：建立 MainWindow、載入 HomeScene、行 event
    // loop、等 ready condition，然後自動退出。回傳 process exit code。
    static int run();

    // atexit 兜底：任何未經 finish() 的退出路徑都補一行 failure result。
    static void reportUnfinishedAtExit();

private slots:
    void onEventLoopEntered();
    void onHomeSceneReady();
    void onHomeSceneFailed(const QString &error);
    void onSettled();
    void onTimeout();

private:
    void emitStage(const QString &stage, bool ok, const QJsonObject &details = QJsonObject());
    int remainingMs() const;
    // 同步階段（engine bootstrap／MainWindow 建構）唔會行 event loop，QTimer 唔會
    // 觸發，所以喺每個同步檢查點主動比對 deadline。
    bool failIfDeadlineExceeded(const QString &stage);
    bool failIfDeadlineExceeded(const QString &stage, bool force);
    void finish(bool ok, const QString &stage, const QString &error,
        UiStartupSmokeReport::ExitCode exitCode);
    void writeReportFile();
    QJsonObject environmentDetails() const;
    int execute();

    static UiStartupSmokeController *s_active;

    QStringList m_arguments;
    int m_timeoutMs = UiStartupSmokeReport::defaultTimeoutMs();
    QString m_reportPath;
    QString m_startupPage = QStringLiteral("home");

    QPointer<MainWindow> m_mainWindow;
    QTimer *m_timeoutTimer = nullptr;
    QElapsedTimer m_elapsed;

    QString m_pendingStage;
    bool m_eventLoopEntered = false;
    bool m_homeSceneReady = false;
    bool m_finished = false;
    int m_exitCode = UiStartupSmokeReport::Passed;

    QJsonArray m_stages;
    QJsonArray m_qtMessages;
    QJsonArray m_optionalAssetWarnings;
    int m_criticalCount = 0;

    friend void uiStartupSmokeMessageHandler(QtMsgType, const QMessageLogContext &,
        const QString &);
};

#endif

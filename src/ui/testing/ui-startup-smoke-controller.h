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

// The real startup smoke for Linux GUI M1.
//
// Unlike --local-response-ui-capabilities (a binary capability query that
// returns before QApplication), this controller always runs the full GUI
// startup path:
//
//     QApplication → engine/runtime → MainWindow → HomeScene/QML → event loop
//
// It does not duplicate a fake HomeScene startup flow; it creates the product's
// own MainWindow directly, judges readiness via MainWindow's home scene signal,
// and then exits on its own.
class UiStartupSmokeController final : public QObject
{
    Q_OBJECT

public:
    explicit UiStartupSmokeController(QObject *parent = nullptr);
    ~UiStartupSmokeController() override;

    static bool isRequested(const QStringList &arguments);

    // Call after QApplication is created and before engine bootstrap: installs
    // the Qt message hook and records the application stage. Returning false
    // means the arguments are invalid (a failure result has been output).
    static bool begin(const QStringList &arguments, int *exitCode);

    // Call when GUI initialization fails midway (e.g. EngineBootstrap): outputs
    // a failure result and returns the matching exit code. If begin() never
    // ran, treat it as no smoke requested and return the fallback.
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
    // Synchronous phases (engine bootstrap / MainWindow construction) never run
    // the event loop, so QTimer will not fire; check the deadline explicitly at
    // each synchronous checkpoint.
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

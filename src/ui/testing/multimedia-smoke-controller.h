#ifndef MULTIMEDIA_SMOKE_CONTROLLER_H
#define MULTIMEDIA_SMOKE_CONTROLLER_H

#include "multimedia-smoke-report.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QStringList>

class MainWindow;
class QTimer;

// The multimedia smoke for Linux GUI M2B-A.
//
// Like the M1 startup smoke, this runs the product's own path: QApplication →
// engine → MainWindow → HomeScene/QML, then drives the real Audio facade inside
// the same process (without spinning up a fake audio system), and finally reads
// the video state reported by HomeController.
//
// CI has neither an audio device nor video assets, so the pass criteria are
// "objects can be created, sources can be set, missing files or devices degrade
// gracefully, clean teardown, no crash or hang" — not "sound is actually heard".
class MultimediaSmokeController final : public QObject
{
    Q_OBJECT

public:
    explicit MultimediaSmokeController(QObject *parent = nullptr);
    ~MultimediaSmokeController() override;

    static bool isRequested(const QStringList &arguments);
    static bool begin(const QStringList &arguments, int *exitCode);
    static int abortEarly(const QString &stage, const QString &error, int fallbackExitCode);
    static int run();
    static void reportUnfinishedAtExit();

private slots:
    void onEventLoopEntered();
    void onHomeSceneReady();
    void onHomeSceneFailed(const QString &error);
    void onTimeout();

    void stageBackend();
    void stageUiEffect();
    void stageVoice();
    void stageBgm();
    void stageMissingAsset();
    void stageVideo();
    void stageShutdown();

private:
    void emitStage(const QString &stage, bool ok, const QJsonObject &details = QJsonObject());
    void failStage(const QString &stage, const QString &error,
        const QJsonObject &details = QJsonObject());
    // 每個 stage 之間都要行返幾轉 event loop：Qt Multimedia 的 state transition
    // 係非同步的，同步連環 call 會驗到一個未 settle 的狀態。
    void scheduleNext(void (MultimediaSmokeController::*slot)(), int delayMs = 120);
    int remainingMs() const;
    bool failIfDeadlineExceeded(const QString &stage, bool force = false);
    void finish(bool ok, const QString &stage, const QString &error,
        MultimediaSmokeReport::ExitCode exitCode);
    void writeReportFile();
    QJsonObject environmentDetails() const;
    QJsonObject audioDiagnostics() const;
    // Fixture directory: tests/fixtures/media/. When it is missing the stage
    // does not fail, it only sets fixture_available=false — a missing fixture
    // must be distinguishable from a broken backend.
    static QString fixturePath(const QString &name);
    int execute();

    static MultimediaSmokeController *s_active;

    QStringList m_arguments;
    int m_timeoutMs = MultimediaSmokeReport::defaultTimeoutMs();
    QString m_reportPath;
    QString m_videoSource;
    QString m_fixtureRoot;

    QPointer<MainWindow> m_mainWindow;
    QTimer *m_timeoutTimer = nullptr;
    QElapsedTimer m_elapsed;

    QString m_pendingStage;
    bool m_eventLoopEntered = false;
    bool m_homeSceneReady = false;
    bool m_finished = false;
    int m_exitCode = MultimediaSmokeReport::Passed;

    QJsonArray m_stages;
    QJsonArray m_qtMessages;
    QJsonArray m_optionalAssetWarnings;
    QJsonObject m_videoResult;
    int m_criticalCount = 0;

    friend void multimediaSmokeMessageHandler(QtMsgType, const QMessageLogContext &,
        const QString &);
};

#endif

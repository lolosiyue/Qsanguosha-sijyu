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

// Multimedia smoke controller for the GUI startup and audio paths.

// Runs the product's QApplication, Engine, MainWindow, HomeScene and Audio facade, then reads the video status from HomeController.




// CI has no audio device or video assets; it verifies resource setup, fallback, teardown, and crash-free completion, not audible output.


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
    // Qt Multimedia state changes are asynchronous; let the event loop advance between stages.

    void scheduleNext(void (MultimediaSmokeController::*slot)(), int delayMs = 120);
    int remainingMs() const;
    bool failIfDeadlineExceeded(const QString &stage, bool force = false);
    void finish(bool ok, const QString &stage, const QString &error,
        MultimediaSmokeReport::ExitCode exitCode);
    void writeReportFile();
    QJsonObject environmentDetails() const;
    QJsonObject audioDiagnostics() const;
    // Fixture directory: tools/ci/fixtures/media/. When it is missing the stage
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

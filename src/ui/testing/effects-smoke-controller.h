#ifndef EFFECTS_SMOKE_CONTROLLER_H
#define EFFECTS_SMOKE_CONTROLLER_H

#include "effects-smoke-report.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QStringList>

class MainWindow;
class QTimer;

// The effects smoke for Linux GUI M2B-B.
//
// Like the M1 startup and M2B-A multimedia smokes, this runs the product's own
// path: QApplication → engine → MainWindow → HomeScene, then queries the
// product's VisualEffectsPolicy and drives the product's effect classes
// (EffectsCompletion, PixmapAnimation, EmotionItem/QMovie, SpineGlItem) inside
// the same process, without spinning up a fake effect system.
//
// CI has neither real art assets nor a GPU, so the pass criteria are "profile
// resolved correctly, feature gates follow the contract, completion delivered
// exactly once, missing or broken assets degrade gracefully, objects that must
// not be created are not created, clean teardown, no crash or hang" — not
// "the visuals look identical".
//
// A full NONE/REDUCED game is verified by tools/autotest/gui_network_smoke.py
// with --effects-profile; this smoke does not duplicate a RoomScene driver.
class EffectsSmokeController final : public QObject
{
    Q_OBJECT

public:
    explicit EffectsSmokeController(QObject *parent = nullptr);
    ~EffectsSmokeController() override;

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

    void stagePolicy();
    void stageCompletion();
    void stageAnimation();
    void stageGif();
    void stageSpine();
    void stageBudget();
    void stageShutdown();

private:
    void emitStage(const QString &stage, bool ok, const QJsonObject &details = QJsonObject());
    void failStage(const QString &stage, const QString &error,
        const QJsonObject &details = QJsonObject());
    // A few event loop turns must run between stages: completion is delivered
    // via queued connections, and synchronous back-to-back calls would only
    // observe a partially delivered state.
    void scheduleNext(void (EffectsSmokeController::*slot)(), int delayMs = 60);
    int remainingMs() const;
    bool failIfDeadlineExceeded(const QString &stage, bool force = false);
    void finish(bool ok, const QString &stage, const QString &error,
        EffectsSmokeReport::ExitCode exitCode);
    void writeReportFile();
    QJsonObject environmentDetails() const;
    QString fixturePath(const QString &relative) const;
    int execute();

    static EffectsSmokeController *s_active;

    QStringList m_arguments;
    int m_timeoutMs = EffectsSmokeReport::defaultTimeoutMs();
    QString m_reportPath;
    QString m_fixtureRoot;

    QPointer<MainWindow> m_mainWindow;
    QTimer *m_timeoutTimer = nullptr;
    QElapsedTimer m_elapsed;

    QString m_pendingStage;
    bool m_eventLoopEntered = false;
    bool m_homeSceneReady = false;
    bool m_finished = false;
    int m_exitCode = EffectsSmokeReport::Passed;

    // The completion stage's results are reported together with the budget stage.
    QJsonObject m_completionResult;
    // Counter snapshot taken before a stage starts, used to tell how many objects this stage created.
    QJsonObject m_countersBeforeAssets;

    QJsonArray m_stages;
    QJsonArray m_qtMessages;
    QJsonArray m_optionalAssetWarnings;
    int m_criticalCount = 0;

    friend void effectsSmokeMessageHandler(QtMsgType, const QMessageLogContext &,
        const QString &);
};

#endif

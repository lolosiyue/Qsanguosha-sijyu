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

// Linux GUI M2B-A 的 multimedia smoke。
//
// 同 M1 的 startup smoke 一樣，行的係產品本身的路徑：QApplication → engine →
// MainWindow → HomeScene/QML，然後喺同一個 process 入面驅動真正的 Audio facade
// （唔會另外開一套假的 audio 系統），最後讀 HomeController 報告返嚟的影片狀態。
//
// CI 上冇音訊裝置亦冇影片資產，所以通過條件係「物件建立得到、來源收得落、
// 缺檔案／缺裝置有降級、關得乾淨、冇 crash／hang」，而唔係「真係聽到聲」。
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
    // fixture 目錄：tests/fixtures/media/。缺失時 stage 唔會失敗，只會標記
    // fixture_available=false —— 缺 fixture 同 backend 壞咗要分得開。
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

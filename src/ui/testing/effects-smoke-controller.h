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

// Linux GUI M2B-B 的 effects smoke。
//
// 同 M1 startup／M2B-A multimedia 一樣，行的係產品本身的路徑：QApplication →
// engine → MainWindow → HomeScene，然後喺同一個 process 入面問產品用嘅
// VisualEffectsPolicy 同驅動產品用嘅效果 class（EffectsCompletion、
// PixmapAnimation、EmotionItem／QMovie、SpineGlItem），唔會另外開一套假嘅
// 效果系統。
//
// CI 上冇正式美術資產亦冇 GPU，所以通過條件係「profile 解析啱、feature gate
// 跟契約、completion exactly once、缺／壞資產降級、唔應該建立嘅物件冇建立、
// 關得乾淨、冇 crash／hang」，而唔係「畫面睇落一樣」。
//
// 完整一局 NONE／REDUCED 對局由 tools/autotest/gui_network_smoke.py 加
// --effects-profile 驗；呢度唔會複製一份 RoomScene 驅動器。
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
    // 每個 stage 之間都要行返幾轉 event loop：completion 係 queued 派送嘅，
    // 同步連環 call 會驗到一個未派完嘅狀態。
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

    // completion stage 嘅結果要留到 budget stage 一齊報。
    QJsonObject m_completionResult;
    // stage 開始之前記低嘅 counter，用嚟分辨「呢個 stage 建立咗幾多」。
    QJsonObject m_countersBeforeAssets;

    QJsonArray m_stages;
    QJsonArray m_qtMessages;
    QJsonArray m_optionalAssetWarnings;
    int m_criticalCount = 0;

    friend void effectsSmokeMessageHandler(QtMsgType, const QMessageLogContext &,
        const QString &);
};

#endif

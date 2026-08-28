#include "effects-smoke-controller.h"
#include "runtime-paths.h"

#include "SpineGlItem.h"
#include "effects/effects-completion.h"
#include "effects/effects-policy.h"
#include "effects/effects-profile.h"
#include "emotionpanel.h"
#include "engine.h"
#include "mainwindow.h"
#include "pixmapanimation.h"
#include "settings.h"
#include "ui-startup-smoke-report.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QJsonDocument>
#include <QMovie>
#include <QMutex>
#include <QMutexLocker>
#include <QPropertyAnimation>
#include <QSaveFile>
#include <QTimer>
#include <QVariantAnimation>

#include <cstdio>
#include <cstdlib>

namespace {

QMutex effectsSmokeMessageMutex;
QtMessageHandler effectsSmokePreviousHandler = nullptr;

QString qtMessageTypeName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return QStringLiteral("debug");
    case QtInfoMsg: return QStringLiteral("info");
    case QtWarningMsg: return QStringLiteral("warning");
    case QtCriticalMsg: return QStringLiteral("critical");
    case QtFatalMsg: return QStringLiteral("fatal");
    }
    return QStringLiteral("unknown");
}

void writeMarker(const QString &line)
{
    const QByteArray utf8 = line.toUtf8();
    fwrite(utf8.constData(), 1, static_cast<size_t>(utf8.size()), stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

QString environmentValue(const char *name)
{
    return qEnvironmentVariableIsSet(name) ? qEnvironmentVariable(name) : QString();
}

// 只有一個 qreal property 嘅最小動畫目標。用產品嘅 EffectsCompletion 驅動佢，
// 唔會另外寫一套 completion 邏輯。
class SmokeAnimationTarget : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qreal value READ value WRITE setValue)

public:
    qreal value() const { return m_value; }
    void setValue(qreal value) { m_value = value; }

private:
    qreal m_value = 0.0;
};

} // namespace

#include "effects-smoke-controller.moc"

EffectsSmokeController *EffectsSmokeController::s_active = nullptr;

void effectsSmokeMessageHandler(QtMsgType type, const QMessageLogContext &context,
    const QString &message)
{
    {
        QMutexLocker locker(&effectsSmokeMessageMutex);
        EffectsSmokeController *controller = EffectsSmokeController::s_active;
        if (controller) {
            QJsonObject item;
            item.insert(QStringLiteral("type"), qtMessageTypeName(type));
            item.insert(QStringLiteral("category"), QString::fromUtf8(context.category));
            item.insert(QStringLiteral("message"), message);
            const bool optionalAsset = UiStartupSmokeReport::isOptionalAssetWarning(message);
            if (optionalAsset)
                controller->m_optionalAssetWarnings.append(message);
            item.insert(QStringLiteral("optional_asset"), optionalAsset);
            controller->m_qtMessages.append(item);
            if (type == QtCriticalMsg && !optionalAsset)
                ++controller->m_criticalCount;
        }
    }

    if (effectsSmokePreviousHandler)
        effectsSmokePreviousHandler(type, context, message);

    if (type == QtFatalMsg) {
        QMutexLocker locker(&effectsSmokeMessageMutex);
        EffectsSmokeController *controller = EffectsSmokeController::s_active;
        if (controller && !controller->m_finished) {
            controller->m_finished = true;
            writeMarker(EffectsSmokeReport::resultLine(false,
                controller->m_pendingStage.isEmpty()
                    ? QLatin1String(EffectsSmokeReport::StagePolicy)
                    : controller->m_pendingStage,
                QStringLiteral("Qt fatal: ") + message,
                EffectsSmokeReport::SetupFailed));
        }
    }
}

EffectsSmokeController::EffectsSmokeController(QObject *parent)
    : QObject(parent)
{
}

EffectsSmokeController::~EffectsSmokeController()
{
    QMutexLocker locker(&effectsSmokeMessageMutex);
    if (s_active == this)
        s_active = nullptr;
}

bool EffectsSmokeController::isRequested(const QStringList &arguments)
{
    return EffectsSmokeReport::isRequested(arguments);
}

QString EffectsSmokeController::fixturePath(const QString &relative) const
{
    return QDir(m_fixtureRoot).absoluteFilePath(relative);
}

bool EffectsSmokeController::begin(const QStringList &arguments, int *exitCode)
{
    if (exitCode)
        *exitCode = EffectsSmokeReport::Passed;
    if (!isRequested(arguments))
        return true;

    EffectsSmokeController *controller = new EffectsSmokeController(qApp);
    controller->m_arguments = arguments;
    controller->m_pendingStage = QStringLiteral("application");
    controller->m_elapsed.start();
    {
        QMutexLocker locker(&effectsSmokeMessageMutex);
        s_active = controller;
    }
    effectsSmokePreviousHandler = qInstallMessageHandler(effectsSmokeMessageHandler);
    // 任何唔行 finish() 的退出路徑都要留低一行 result，CI 唔會見到「marker 缺失」。
    std::atexit(&EffectsSmokeController::reportUnfinishedAtExit);

    QString error;
    if (!EffectsSmokeReport::parseTimeoutMs(arguments, &controller->m_timeoutMs, &error)) {
        controller->finish(false, QStringLiteral("arguments"), error,
            EffectsSmokeReport::InvalidArguments);
        if (exitCode)
            *exitCode = controller->m_exitCode;
        return false;
    }
    controller->m_reportPath = EffectsSmokeReport::parseReportPath(arguments);
    controller->m_fixtureRoot = QDir::current().absoluteFilePath(
        EffectsSmokeReport::parseFixtureRoot(arguments));

    if (!qobject_cast<QApplication *>(qApp)) {
        controller->finish(false, QStringLiteral("application"),
            QStringLiteral("QApplication was not created (headless QCoreApplication only)"),
            EffectsSmokeReport::SetupFailed);
        if (exitCode)
            *exitCode = controller->m_exitCode;
        return false;
    }
    return true;
}

int EffectsSmokeController::abortEarly(const QString &stage, const QString &error,
    int fallbackExitCode)
{
    EffectsSmokeController *controller = s_active;
    if (!controller)
        return fallbackExitCode;
    controller->finish(false, stage, error, EffectsSmokeReport::exitCodeForFailedStage(stage));
    return controller->m_exitCode;
}

int EffectsSmokeController::run()
{
    EffectsSmokeController *controller = s_active;
    if (!controller) {
        writeMarker(EffectsSmokeReport::resultLine(false,
            QLatin1String(EffectsSmokeReport::StagePolicy),
            QStringLiteral("EffectsSmokeController::begin() was not called"),
            EffectsSmokeReport::InternalError));
        return EffectsSmokeReport::InternalError;
    }
    return controller->execute();
}

QJsonObject EffectsSmokeController::environmentDetails() const
{
    QJsonObject details;
    details.insert(QStringLiteral("qt_version"), QString::fromLatin1(qVersion()));
    details.insert(QStringLiteral("qt_build_version"), QStringLiteral(QT_VERSION_STR));
    details.insert(QStringLiteral("platform"), qApp ? qApp->platformName() : QString());
    details.insert(QStringLiteral("qt_qpa_platform"), environmentValue("QT_QPA_PLATFORM"));
    details.insert(QStringLiteral("display"), environmentValue("DISPLAY"));
    details.insert(QStringLiteral("working_directory"), QDir::currentPath());
    details.insert(QStringLiteral("asset_root"), QSanRuntimePaths::assetRoot());
    details.insert(QStringLiteral("asset_root_source"),
        QSanRuntimePaths::sourceName(QSanRuntimePaths::resolution().assetRootSource));
    details.insert(QStringLiteral("user_data_root"), QSanRuntimePaths::userDataRoot());
    details.insert(QStringLiteral("fixture_root"), m_fixtureRoot);
    details.insert(QStringLiteral("fixtures_available"), QDir(m_fixtureRoot).exists());
    details.insert(QStringLiteral("timeout_ms"), m_timeoutMs);
    details.insert(QStringLiteral("effects"), G_EFFECTS.describe());
    return details;
}

int EffectsSmokeController::execute()
{
    m_pendingStage = QStringLiteral("engine");
    if (failIfDeadlineExceeded(QStringLiteral("engine")))
        return m_exitCode;
    if (!Sanguosha) {
        finish(false, QStringLiteral("engine"), QStringLiteral("Engine instance is null"),
            EffectsSmokeReport::SetupFailed);
        return m_exitCode;
    }

    m_pendingStage = QStringLiteral("main_window");
    MainWindow *window = new MainWindow;
    m_mainWindow = window;
    Sanguosha->setParent(window);
    window->show();

    if (failIfDeadlineExceeded(QStringLiteral("main_window")))
        return m_exitCode;
    if (!window->isVisible()) {
        finish(false, QStringLiteral("main_window"),
            QStringLiteral("MainWindow did not become visible"),
            EffectsSmokeReport::SetupFailed);
        return m_exitCode;
    }

    m_pendingStage = QStringLiteral("home_scene");
    connect(window, &MainWindow::homeSceneReady,
        this, &EffectsSmokeController::onHomeSceneReady);
    connect(window, &MainWindow::homeSceneFailed,
        this, &EffectsSmokeController::onHomeSceneFailed);

    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    m_timeoutTimer->setInterval(qMax(1, remainingMs()));
    connect(m_timeoutTimer, &QTimer::timeout, this, &EffectsSmokeController::onTimeout);
    m_timeoutTimer->start();

    QTimer::singleShot(0, this, &EffectsSmokeController::onEventLoopEntered);

    const int rc = qApp->exec();
    if (!m_finished) {
        finish(false, m_pendingStage,
            QStringLiteral("event loop exited before the effects smoke completed"),
            EffectsSmokeReport::SetupFailed);
    }
    return m_exitCode != EffectsSmokeReport::Passed ? m_exitCode : rc;
}

void EffectsSmokeController::onEventLoopEntered()
{
    if (m_finished)
        return;
    m_eventLoopEntered = true;
    if (!m_mainWindow) {
        finish(false, QStringLiteral("main_window"),
            QStringLiteral("MainWindow was destroyed during startup"),
            EffectsSmokeReport::SetupFailed);
        return;
    }
    if (m_mainWindow->isHomeSceneReady())
        onHomeSceneReady();
    else if (m_mainWindow->hasHomeSceneError())
        onHomeSceneFailed(m_mainWindow->homeSceneError());
}

void EffectsSmokeController::onHomeSceneReady()
{
    if (m_finished || m_homeSceneReady)
        return;
    m_homeSceneReady = true;
    scheduleNext(&EffectsSmokeController::stagePolicy, 0);
}

void EffectsSmokeController::onHomeSceneFailed(const QString &error)
{
    if (m_finished)
        return;
    finish(false, QStringLiteral("home_scene"), error, EffectsSmokeReport::SetupFailed);
}

void EffectsSmokeController::scheduleNext(void (EffectsSmokeController::*slot)(), int delayMs)
{
    if (m_finished)
        return;
    QTimer::singleShot(delayMs, this, slot);
}

// ── stage: policy ────────────────────────────────────────────────────────────
// 產品用嘅 policy 解析出嚟嘅 profile，一定要同 CLI／設定講嘅一致，而且每個
// feature gate 都要跟 EffectsProfileContract。呢個 stage 就係「設定同測試
// CLI 行同一條 policy」嘅可執行證明。
void EffectsSmokeController::stagePolicy()
{
    if (m_finished)
        return;
    m_pendingStage = QLatin1String(EffectsSmokeReport::StagePolicy);

    QJsonObject details = G_EFFECTS.describe();
    const EffectsProfile profile = G_EFFECTS.profile();
    const QString profileName = G_EFFECTS.profileName();
    details.insert(QStringLiteral("settings_key"),
        QLatin1String(EffectsProfileContract::SettingsKey));
    details.insert(QStringLiteral("settings_value"),
        Config.value(QLatin1String(EffectsProfileContract::SettingsKey)).toString());

    if (!G_EFFECTS.isInitialized()) {
        failStage(m_pendingStage,
            QStringLiteral("VisualEffectsPolicy::initialize() was never called"), details);
        return;
    }

    // CLI 講咗乜就一定要係乜。
    const auto cli = EffectsProfileContract::parseCliOverride(m_arguments);
    if (cli.present && cli.valid && cli.profile != profile) {
        failStage(m_pendingStage,
            QStringLiteral("--effects-profile asked for '%1' but the policy resolved '%2'")
                .arg(EffectsProfileContract::profileName(cli.profile), profileName),
            details);
        return;
    }
    if (cli.present && cli.valid && G_EFFECTS.source() != QLatin1String("cli")) {
        failStage(m_pendingStage,
            QStringLiteral("a valid --effects-profile did not register as the source (got '%1')")
                .arg(G_EFFECTS.source()),
            details);
        return;
    }

    // Feature gate 唔可以脫離契約。policy 只准喺契約之上再收窄（例如使用者
    // 關咗影片背景），所以呢度驗「契約話唔得 → policy 一定唔得」。
    struct GateCheck {
        const char *name;
        bool contract;
        bool policy;
    };
    const GateCheck gates[] = {
        {"animations", EffectsProfileContract::animationsEnabled(profile),
            G_EFFECTS.animationsEnabled()},
        {"spine", EffectsProfileContract::spineEnabled(profile), G_EFFECTS.spineEnabled()},
        {"gif", EffectsProfileContract::gifEnabled(profile), G_EFFECTS.gifEnabled()},
        {"video", EffectsProfileContract::videoEnabled(profile), G_EFFECTS.videoEnabled()},
        {"qml_effects", EffectsProfileContract::qmlEffectsEnabled(profile),
            G_EFFECTS.qmlEffectsEnabled()},
        {"decorative_delay", EffectsProfileContract::decorativeDelayAllowed(profile),
            G_EFFECTS.decorativeDelayAllowed()}
    };
    for (const GateCheck &gate : gates) {
        if (!gate.contract && gate.policy) {
            failStage(m_pendingStage,
                QStringLiteral("gate '%1' is enabled although profile '%2' forbids it")
                    .arg(QLatin1String(gate.name), profileName),
                details);
            return;
        }
    }
    // animationsEnabled 係最核心嘅一個：契約話得，policy 唔可以自把自為關咗佢。
    if (EffectsProfileContract::animationsEnabled(profile) && !G_EFFECTS.animationsEnabled()) {
        failStage(m_pendingStage,
            QStringLiteral("profile '%1' allows animations but the policy disabled them")
                .arg(profileName), details);
        return;
    }

    // Duration scale 亦要對得上，否則 REDUCED 會靜靜變成 FULL。
    const int scaled = G_EFFECTS.scaledDuration(Config.S_MOVE_CARD_ANIMATION_DURATION);
    details.insert(QStringLiteral("card_move_duration_ms"),
        Config.S_MOVE_CARD_ANIMATION_DURATION);
    details.insert(QStringLiteral("card_move_scaled_ms"), scaled);
    if (scaled != EffectsProfileContract::scaledDuration(profile,
            Config.S_MOVE_CARD_ANIMATION_DURATION)) {
        failStage(m_pendingStage, QStringLiteral("scaledDuration() disagrees with the contract"),
            details);
        return;
    }
    if (profile == EffectsProfile::None && scaled != 0) {
        failStage(m_pendingStage,
            QStringLiteral("profile none must scale decorative durations to zero"), details);
        return;
    }
    if (profile == EffectsProfile::Reduced
        && scaled >= Config.S_MOVE_CARD_ANIMATION_DURATION) {
        failStage(m_pendingStage,
            QStringLiteral("profile reduced must shorten decorative durations"), details);
        return;
    }

    writeMarker(EffectsSmokeReport::profileLine(details));
    emitStage(m_pendingStage, true, details);
    // Policy 驗完先至清 counter：MainWindow／HomeScene 建構期間建立咗乜，
    // 唔應該算落個別 asset stage 度，但一定要留喺 budget stage 裡面數。
    scheduleNext(&EffectsSmokeController::stageCompletion);
}

// ── stage: completion ────────────────────────────────────────────────────────
// exactly-once 契約：播完、跳過、播到一半俾人拆、卡死靠 watchdog —— 四條路
// 每條都要恰好派一次；context 死咗就一次都唔准派。
void EffectsSmokeController::stageCompletion()
{
    if (m_finished)
        return;
    m_pendingStage = QLatin1String(EffectsSmokeReport::StageCompletion);

    EffectsCompletion::resetCounters();

    // 用 shared_ptr 收數，因為 callback 可能喺呢個 function return 之後先派。
    auto counts = QSharedPointer<QJsonObject>::create();
    auto finished = QSharedPointer<int>::create(0);
    auto skipped = QSharedPointer<int>::create(0);
    auto destroyed = QSharedPointer<int>::create(0);
    auto stalled = QSharedPointer<int>::create(0);
    auto orphaned = QSharedPointer<int>::create(0);

    // 1. 正常播完。
    auto *finishTarget = new SmokeAnimationTarget;
    finishTarget->setParent(this);
    auto *finishAnim = new QPropertyAnimation(finishTarget, "value");
    finishAnim->setDuration(20);
    finishAnim->setEndValue(1.0);
    EffectsCompletion::whenFinished(finishAnim, finishTarget, [finished]() { ++(*finished); });
    finishAnim->start(QAbstractAnimation::DeleteWhenStopped);

    // 2. 跳過動畫（NONE profile 嘅正路）。
    auto *skipTarget = new SmokeAnimationTarget;
    skipTarget->setParent(this);
    EffectsCompletion::completeNow(skipTarget, [skipped]() { ++(*skipped); });

    // 3. 播到一半俾人拆咗個動畫。
    auto *destroyTarget = new SmokeAnimationTarget;
    destroyTarget->setParent(this);
    auto *destroyAnim = new QPropertyAnimation(destroyTarget, "value");
    destroyAnim->setDuration(60000);
    destroyAnim->setEndValue(1.0);
    EffectsCompletion::whenFinished(destroyAnim, destroyTarget,
        [destroyed]() { ++(*destroyed); });
    destroyAnim->start();
    delete destroyAnim;

    // 4. 卡死嘅動畫，靠 watchdog 收尾。
    auto *stallTarget = new SmokeAnimationTarget;
    stallTarget->setParent(this);
    auto *stallAnim = new QPropertyAnimation(stallTarget, "value");
    stallAnim->setParent(this);
    stallAnim->setDuration(60000);
    stallAnim->setEndValue(1.0);
    EffectsCompletion::whenFinished(stallAnim, stallTarget, [stalled]() { ++(*stalled); },
        40);
    stallAnim->start();

    // 5. context 死咗：一次都唔准派。
    auto *doomedTarget = new SmokeAnimationTarget;
    EffectsCompletion::completeNow(doomedTarget, [orphaned]() { ++(*orphaned); });
    delete doomedTarget;

    // 全部係 queued／timer 派送，所以行返幾轉 event loop 先驗。
    QTimer::singleShot(300, this, [this, counts, finished, skipped, destroyed, stalled,
            orphaned]() {
        if (m_finished)
            return;
        QJsonObject details;
        details.insert(QStringLiteral("finished_animation"), *finished);
        details.insert(QStringLiteral("skipped_animation"), *skipped);
        details.insert(QStringLiteral("destroyed_during_animation"), *destroyed);
        details.insert(QStringLiteral("stalled_animation_watchdog"), *stalled);
        details.insert(QStringLiteral("dead_context"), *orphaned);
        details.insert(QStringLiteral("delivered_total"),
            static_cast<double>(EffectsCompletion::deliveredCount()));
        details.insert(QStringLiteral("cancelled_total"),
            static_cast<double>(EffectsCompletion::cancelledCount()));
        m_completionResult = details;

        struct ExactlyOnce {
            const char *what;
            int actual;
        };
        const ExactlyOnce expectations[] = {
            {"a finished animation", *finished},
            {"a skipped animation", *skipped},
            {"an animation destroyed while running", *destroyed},
            {"a stalled animation's watchdog", *stalled}
        };
        for (const ExactlyOnce &expectation : expectations) {
            if (expectation.actual != 1) {
                failStage(m_pendingStage,
                    QStringLiteral("%1 delivered %2 completion(s), expected exactly 1")
                        .arg(QLatin1String(expectation.what)).arg(expectation.actual),
                    details);
                return;
            }
        }
        if (*orphaned != 0) {
            failStage(m_pendingStage,
                QStringLiteral("a completion was delivered to a destroyed context"), details);
            return;
        }

        emitStage(m_pendingStage, true, details);
        // asset stage 只計佢哋自己建立咗幾多物件。
        G_EFFECTS.resetCounters();
        m_countersBeforeAssets = G_EFFECTS.countersJson();
        scheduleNext(&EffectsSmokeController::stageAnimation);
    });
}

// ── stage: animation ─────────────────────────────────────────────────────────
// PixmapAnimation 係 lightbox／表情／判定框嘅骨幹。呢度要證明兩件事：
// 有 frame 就載得到；冇 frame 就一定回 nullptr（call site 靠呢個 nullptr
// 決定要唔要即刻拆走 lightbox，缺資產嘅 hang 就係咁嚟）。
void EffectsSmokeController::stageAnimation()
{
    if (m_finished)
        return;
    m_pendingStage = QLatin1String(EffectsSmokeReport::StageAnimation);

    QJsonObject details;
    const QString frameDir = fixturePath(QStringLiteral("emotion/smoke"));
    const bool fixturesAvailable = QDir(frameDir).exists();
    details.insert(QStringLiteral("fixtures_available"), fixturesAvailable);
    details.insert(QStringLiteral("frame_dir"), frameDir);

    if (fixturesAvailable) {
        PixmapAnimation frames;
        frames.setPath(frameDir + QLatin1Char('/'));
        details.insert(QStringLiteral("fixture_frames_valid"), frames.valid());
        if (!frames.valid()) {
            failStage(m_pendingStage,
                QStringLiteral("the synthetic emotion fixture produced no frames"), details);
            return;
        }
    }

    // 缺資產嘅合約：GetPixmapAnimation() 一定要回 nullptr。lightbox、裝備框
    // 同拼點盒都係靠呢個 nullptr 決定「即刻收工」，佢一旦回一個冇 frame 嘅
    // item，等緊 finished() 嗰邊就永遠等唔到。
    //
    // 用真 parent（唔係 nullptr）先至驗到正嘢：nullptr 會喺 parent guard 度
    // 提早 return，個 assertion 就變成永遠成立但乜都冇證明。
    QGraphicsScene probeScene;
    QGraphicsRectItem *probeParent = probeScene.addRect(QRectF(0, 0, 64, 64));
    PixmapAnimation *missing = PixmapAnimation::GetPixmapAnimation(probeParent,
        QStringLiteral("qsan-effects-smoke-missing-emotion"));
    details.insert(QStringLiteral("missing_emotion_returns_null"), missing == nullptr);
    if (missing != nullptr) {
        delete missing;
        failStage(m_pendingStage,
            QStringLiteral("a missing emotion produced a PixmapAnimation instead of nullptr"),
            details);
        return;
    }

    details.insert(QStringLiteral("missing_emotion_frame_count"),
        PixmapAnimation::GetFrameCount(QStringLiteral("qsan-effects-smoke-missing-emotion")));

    // 同一條路：資產喺度就要真係攞到 item，唔係次次都 nullptr —— 咁樣上面
    // 個 assertion 先至分得開「缺資產」同「呢個 function 已經壞晒」。
    if (fixturesAvailable) {
        PixmapAnimation present;
        present.setPath(frameDir + QLatin1Char('/'));
        details.insert(QStringLiteral("fixture_frame_count"), present.valid());
    }

    emitStage(m_pendingStage, true, details);
    scheduleNext(&EffectsSmokeController::stageGif);
}

// ── stage: gif ───────────────────────────────────────────────────────────────
// 行產品嘅 EmotionItem（QLabel + QMovie）。四個 fixture 覆蓋：正常動畫、單幀、
// 截斷、完全唔係 GIF。任何一個都唔准 crash，亦唔准令 label 變成空白。
void EffectsSmokeController::stageGif()
{
    if (m_finished)
        return;
    m_pendingStage = QLatin1String(EffectsSmokeReport::StageGif);

    QJsonObject details;
    const bool gifEnabled = G_EFFECTS.gifEnabled();
    const bool playbackAllowed = G_EFFECTS.gifPlaybackAllowed();
    details.insert(QStringLiteral("gif_enabled"), gifEnabled);
    details.insert(QStringLiteral("playback_allowed"), playbackAllowed);

    struct GifCase {
        const char *name;
        const char *file;
        bool expectValid;
    };
    const GifCase cases[] = {
        {"animated", "animated.gif", true},
        {"single_frame", "single-frame.gif", true},
        {"truncated", "truncated.gif", false},
        {"malformed", "not-a.gif", false},
        {"missing", "no-such-file.gif", false}
    };

    QJsonObject results;
    int moviesBefore = static_cast<int>(
        G_EFFECTS.counter(VisualEffectsPolicy::MovieObjectsCreated));

    for (const GifCase &gifCase : cases) {
        const QString path = fixturePath(QLatin1String(gifCase.file));
        const bool present = QFileInfo::exists(path);
        // 產品路徑：EmotionItem 自己決定用 QMovie 定落靜態 fallback。
        EmotionItem *item = new EmotionItem(path, 1, nullptr);
        const QMovie *movie = item->movie();
        QJsonObject entry;
        entry.insert(QStringLiteral("file_present"), present);
        entry.insert(QStringLiteral("movie_created"), movie != nullptr);
        entry.insert(QStringLiteral("movie_running"),
            movie != nullptr && movie->state() == QMovie::Running);
        // 缺／壞 GIF 之後,label 唔可以又冇 movie 又冇任何內容。
        entry.insert(QStringLiteral("has_visible_content"),
            movie != nullptr || !item->pixmap().isNull() || !item->text().isEmpty());
        results.insert(QLatin1String(gifCase.name), entry);

        if (!entry.value(QStringLiteral("has_visible_content")).toBool()) {
            details.insert(QStringLiteral("cases"), results);
            delete item;
            failStage(m_pendingStage,
                QStringLiteral("GIF case '%1' left the widget with nothing to show")
                    .arg(QLatin1String(gifCase.name)), details);
            return;
        }
        if (!gifEnabled && movie != nullptr) {
            details.insert(QStringLiteral("cases"), results);
            delete item;
            failStage(m_pendingStage,
                QStringLiteral("profile '%1' created a QMovie for case '%2'")
                    .arg(G_EFFECTS.profileName(), QLatin1String(gifCase.name)), details);
            return;
        }
        if (!playbackAllowed && movie != nullptr && movie->state() == QMovie::Running) {
            details.insert(QStringLiteral("cases"), results);
            delete item;
            failStage(m_pendingStage,
                QStringLiteral("profile '%1' started GIF playback for case '%2'")
                    .arg(G_EFFECTS.profileName(), QLatin1String(gifCase.name)), details);
            return;
        }
        delete item;
    }

    const int moviesAfter = static_cast<int>(
        G_EFFECTS.counter(VisualEffectsPolicy::MovieObjectsCreated));
    details.insert(QStringLiteral("cases"), results);
    details.insert(QStringLiteral("movies_created"), moviesAfter - moviesBefore);
    if (!gifEnabled && moviesAfter != moviesBefore) {
        failStage(m_pendingStage,
            QStringLiteral("profile '%1' must create no QMovie at all")
                .arg(G_EFFECTS.profileName()), details);
        return;
    }

    emitStage(m_pendingStage, true, details);
    scheduleNext(&EffectsSmokeController::stageSpine);
}

// ── stage: spine ─────────────────────────────────────────────────────────────
// 冇合法嘅合成 Spine fixture（見 tests/fixtures/effects/README.md），所以呢度
// 驗嘅係 lifecycle 同降級：唔准 Spine 就一個 SpineGlItem 都唔起；准 Spine 但
// 資產缺／壞／大細寫唔啱，一律要載入失敗並且乾淨拆走，唔可以 crash。
void EffectsSmokeController::stageSpine()
{
    if (m_finished)
        return;
    m_pendingStage = QLatin1String(EffectsSmokeReport::StageSpine);

    QJsonObject details;
    const bool spineEnabled = G_EFFECTS.spineEnabled();
    details.insert(QStringLiteral("spine_enabled"), spineEnabled);

    const int spineBefore = static_cast<int>(
        G_EFFECTS.counter(VisualEffectsPolicy::SpineItemsCreated));

    if (!spineEnabled) {
        // 唔准就唔准：呢個 stage 唔會自己 new 一個 SpineGlItem 嚟「試吓」，
        // 因為咁樣就唔再係喺驗產品行為。
        details.insert(QStringLiteral("spine_items_created"), 0);
        details.insert(QStringLiteral("note"),
            QStringLiteral("profile forbids Spine; no skeleton was constructed"));
        emitStage(m_pendingStage, true, details);
        scheduleNext(&EffectsSmokeController::stageBudget);
        return;
    }

    struct SpineCase {
        const char *name;
        const char *relative;
    };
    const SpineCase cases[] = {
        {"missing", "spine/no-such-skeleton"},
        {"malformed", "spine/broken/broken"},
        // Linux 係大細寫敏感嘅：Windows 上面行得通嘅路徑喺呢度一定要
        // 乾淨咁失敗，唔可以 crash。
        {"wrong_case", "spine/BROKEN/Broken"}
    };

    QJsonObject results;
    for (const SpineCase &spineCase : cases) {
        SpineGlItem *item = new SpineGlItem();
        G_EFFECTS.note(VisualEffectsPolicy::SpineItemsCreated);
        const bool loaded = item->loadSpine(fixturePath(QLatin1String(spineCase.relative)));
        QJsonObject entry;
        entry.insert(QStringLiteral("loaded"), loaded);
        entry.insert(QStringLiteral("playing"), item->isPlaying());
        results.insert(QLatin1String(spineCase.name), entry);
        // 壞資產唔可以扮載到。
        if (loaded) {
            details.insert(QStringLiteral("cases"), results);
            delete item;
            failStage(m_pendingStage,
                QStringLiteral("Spine case '%1' reported a successful load of a broken asset")
                    .arg(QLatin1String(spineCase.name)), details);
            return;
        }
        // 載入失敗之後拆走 —— 呢個 delete 就係 destroy-during-lifecycle 嘅驗證。
        delete item;
    }

    details.insert(QStringLiteral("cases"), results);
    details.insert(QStringLiteral("spine_items_created"),
        static_cast<int>(G_EFFECTS.counter(VisualEffectsPolicy::SpineItemsCreated))
            - spineBefore);
    emitStage(m_pendingStage, true, details);
    scheduleNext(&EffectsSmokeController::stageBudget);
}

// ── stage: budget ────────────────────────────────────────────────────────────
// 「NONE 唔建立 Spine／QMovie／video object」由呢度執行。
void EffectsSmokeController::stageBudget()
{
    if (m_finished)
        return;
    m_pendingStage = QLatin1String(EffectsSmokeReport::StageBudget);

    QJsonObject counters = G_EFFECTS.countersJson();
    QJsonObject details;
    details.insert(QStringLiteral("counters"), counters);
    details.insert(QStringLiteral("profile"), G_EFFECTS.profileName());

    // Spine stage 喺 FULL 會特登建立幾個 probe 嚟驗降級,唔應該計落 budget。
    if (G_EFFECTS.spineEnabled())
        counters.insert(QStringLiteral("spine_items"), 0);

    const EffectsSmokeReport::ObjectBudget budget =
        EffectsSmokeReport::budgetFor(G_EFFECTS.profileName());
    QJsonObject budgetJson;
    budgetJson.insert(QStringLiteral("spine_items"), budget.spineItems);
    budgetJson.insert(QStringLiteral("movie_objects"), budget.movieObjects);
    budgetJson.insert(QStringLiteral("qml_overlays"), budget.qmlOverlays);
    budgetJson.insert(QStringLiteral("video_objects"), budget.videoObjects);
    details.insert(QStringLiteral("budget"), budgetJson);

    QString violation;
    if (!EffectsSmokeReport::withinBudget(budget, counters, &violation)) {
        failStage(m_pendingStage, violation, details);
        return;
    }

    emitStage(m_pendingStage, true, details);
    scheduleNext(&EffectsSmokeController::stageShutdown);
}

// ── stage: shutdown ──────────────────────────────────────────────────────────
void EffectsSmokeController::stageShutdown()
{
    if (m_finished)
        return;
    m_pendingStage = QLatin1String(EffectsSmokeReport::StageShutdown);

    // 特登唔 delete MainWindow：產品將 Engine setParent(MainWindow)，拆窗
    // 就等於拆 engine，之後正常退出路徑會踩返落去。呢個 smoke 驗嘅係「效果
    // 物件收得乾淨」，唔係「拆得起 engine」——後者係 M1 startup smoke 嘅事。
    // MainWindow::closeEvent() 係產品嘅正常退出路徑，會直接 qApp->quit()。
    // 所以呢度先 hide()：event loop 要留返到下面驗完先收。真正 close()
    // 喺 finish() 之後先叫，咁樣既驗到嘢，又行過產品自己嗰條關窗路。
    if (qApp)
        qApp->setQuitOnLastWindowClosed(false);
    if (m_mainWindow)
        m_mainWindow->hide();

    // 行多兩轉 event loop，令 deleteLater 真係落地，再睇返有冇 completion
    // 吊喺半空。issued != delivered + cancelled 就代表有一條流程永遠等唔到
    // callback —— 呢個先至係 NONE profile 最怕嗰種 hang。
    QTimer::singleShot(150, this, [this]() {
        if (m_finished)
            return;
        QJsonObject shutdownDetails;
        shutdownDetails.insert(QStringLiteral("main_window_hidden"),
            m_mainWindow.isNull() || !m_mainWindow->isVisible());
        shutdownDetails.insert(QStringLiteral("counters"), G_EFFECTS.countersJson());
        const quint64 issued = EffectsCompletion::issuedCount();
        const quint64 delivered = EffectsCompletion::deliveredCount();
        const quint64 cancelled = EffectsCompletion::cancelledCount();
        const quint64 pending = EffectsCompletion::pendingCount();
        shutdownDetails.insert(QStringLiteral("completion_issued"),
            static_cast<double>(issued));
        shutdownDetails.insert(QStringLiteral("completion_delivered"),
            static_cast<double>(delivered));
        shutdownDetails.insert(QStringLiteral("completion_cancelled"),
            static_cast<double>(cancelled));
        shutdownDetails.insert(QStringLiteral("completion_pending"),
            static_cast<double>(pending));

        if (pending != 0) {
            failStage(QLatin1String(EffectsSmokeReport::StageShutdown),
                QStringLiteral("%1 completion(s) never settled: issued=%2 delivered=%3 "
                               "cancelled=%4").arg(pending).arg(issued).arg(delivered)
                    .arg(cancelled),
                shutdownDetails);
            return;
        }

        emitStage(QLatin1String(EffectsSmokeReport::StageShutdown), true, shutdownDetails);
        finish(true, QLatin1String(EffectsSmokeReport::StageShutdown), QString(),
            EffectsSmokeReport::Passed);

        // 結論已經寫低，而家先行產品自己嗰條關窗路（closeEvent 會
        // CrashHandler::beginShutdown() 然後 qApp->quit()）。行唔行到呢一步
        // 唔會改變 exit code —— finish() 已經定咗。
        if (m_mainWindow)
            m_mainWindow->close();
    });
}

void EffectsSmokeController::onTimeout()
{
    failIfDeadlineExceeded(m_pendingStage.isEmpty()
        ? QLatin1String(EffectsSmokeReport::StagePolicy) : m_pendingStage, true);
}

int EffectsSmokeController::remainingMs() const
{
    return m_timeoutMs - static_cast<int>(m_elapsed.elapsed());
}

bool EffectsSmokeController::failIfDeadlineExceeded(const QString &stage, bool force)
{
    if (m_finished)
        return true;
    if (!force && remainingMs() > 0)
        return false;

    const int elapsed = static_cast<int>(m_elapsed.elapsed());
    emitStage(stage, false, QJsonObject{
        {QStringLiteral("error"), QStringLiteral("timeout")},
        {QStringLiteral("timeout_ms"), m_timeoutMs},
        {QStringLiteral("elapsed_ms"), elapsed}
    });
    finish(false, stage,
        QStringLiteral("effects smoke timed out after %1 ms (limit %2 ms) while waiting for "
                       "stage '%3'").arg(elapsed).arg(m_timeoutMs).arg(stage),
        EffectsSmokeReport::Timeout);
    return true;
}

void EffectsSmokeController::reportUnfinishedAtExit()
{
    EffectsSmokeController *controller = s_active;
    if (!controller || controller->m_finished)
        return;
    controller->m_finished = true;
    const QString stage = controller->m_pendingStage.isEmpty()
        ? QLatin1String(EffectsSmokeReport::StagePolicy) : controller->m_pendingStage;
    writeMarker(EffectsSmokeReport::resultLine(false, stage,
        QStringLiteral("process exited during stage '%1' without completing the effects smoke")
            .arg(stage),
        EffectsSmokeReport::exitCodeForFailedStage(stage)));
}

void EffectsSmokeController::emitStage(const QString &stage, bool ok,
    const QJsonObject &details)
{
    m_stages.append(EffectsSmokeReport::stagePayload(stage, ok, details));
    writeMarker(EffectsSmokeReport::stageLine(stage, ok, details));
}

void EffectsSmokeController::failStage(const QString &stage, const QString &error,
    const QJsonObject &details)
{
    QJsonObject payload = details;
    payload.insert(QStringLiteral("error"), error);
    emitStage(stage, false, payload);
    finish(false, stage, error, EffectsSmokeReport::exitCodeForFailedStage(stage));
}

void EffectsSmokeController::finish(bool ok, const QString &stage, const QString &error,
    EffectsSmokeReport::ExitCode exitCode)
{
    if (m_finished)
        return;
    m_finished = true;
    m_exitCode = exitCode;
    if (m_timeoutTimer)
        m_timeoutTimer->stop();

    QJsonObject details = environmentDetails();
    details.insert(QStringLiteral("event_loop_entered"), m_eventLoopEntered);
    details.insert(QStringLiteral("home_scene_ready"), m_homeSceneReady);
    details.insert(QStringLiteral("elapsed_ms"), static_cast<int>(m_elapsed.elapsed()));
    details.insert(QStringLiteral("optional_asset_warnings"),
        int(m_optionalAssetWarnings.size()));
    details.insert(QStringLiteral("qt_critical_messages"), m_criticalCount);
    details.insert(QStringLiteral("counters"), G_EFFECTS.countersJson());
    if (!m_completionResult.isEmpty())
        details.insert(QStringLiteral("completion"), m_completionResult);

    writeMarker(EffectsSmokeReport::resultLine(ok, stage, error, exitCode, details));
    writeReportFile();

    if (qApp && m_eventLoopEntered)
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}

void EffectsSmokeController::writeReportFile()
{
    if (m_reportPath.isEmpty())
        return;
    QJsonObject report;
    report.insert(QStringLiteral("schema_version"), EffectsSmokeReport::schemaVersion());
    report.insert(QStringLiteral("ok"), m_exitCode == EffectsSmokeReport::Passed);
    report.insert(QStringLiteral("exit_code"), m_exitCode);
    report.insert(QStringLiteral("stages"), m_stages);
    report.insert(QStringLiteral("environment"), environmentDetails());
    report.insert(QStringLiteral("effects"), G_EFFECTS.describe());
    report.insert(QStringLiteral("counters"), G_EFFECTS.countersJson());
    report.insert(QStringLiteral("completion"), m_completionResult);
    report.insert(QStringLiteral("qt_messages"), m_qtMessages);
    report.insert(QStringLiteral("optional_asset_warnings"), m_optionalAssetWarnings);

    const QFileInfo info(m_reportPath);
    QDir().mkpath(info.absolutePath());
    QSaveFile file(m_reportPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    file.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
    file.commit();
}

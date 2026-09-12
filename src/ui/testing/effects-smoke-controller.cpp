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

// Minimal animation target with a single qreal property. Drives it through the
// product's EffectsCompletion instead of building a separate completion path.
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
    // Every exit path that does not go through finish() must still leave a result line; CI never sees a "missing marker".
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
    // Destroy the window - and with it the QQuickWidget's QML engine - while QApplication is
    // still alive.  The QML type loader thread resolves its disk cache path through
    // QStandardPaths::writableLocation(), which dereferences the application object, so an
    // engine that outlives main() segfaults whenever a load is still in flight (reproducible
    // with a cold QML cache: a fresh HOME crashed every run, a warm one almost never).
    delete m_mainWindow.data();
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
// The profile resolved by the product's policy must match what the CLI and the
// settings claim, and every feature gate must follow EffectsProfileContract.
// This stage is the executable proof that "settings and the test CLI run the
// same policy".
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

    // Whatever the CLI claims must be exactly what happens.
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

    // Feature gates must not deviate from the contract. The policy may only
    // narrow further on top of the contract (e.g. the user disabled the video
    // background), so this verifies "contract says no -> policy must say no".
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
    // animationsEnabled is the most critical one: when the contract allows it, the policy must not disable it on its own.
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
    // Only clear the counters after the policy stage: whatever MainWindow or
    // HomeScene construction created must not be charged to individual asset
    // stages, but it must still be counted in the budget stage.
    scheduleNext(&EffectsSmokeController::stageCompletion);
}

// ── stage: completion ────────────────────────────────────────────────────────
// exactly-once contract: played to the end, skipped, destroyed mid-play, or
// stuck and reaped by the watchdog — each of the four paths must deliver
// exactly once; once the context is dead, none may deliver at all.
void EffectsSmokeController::stageCompletion()
{
    if (m_finished)
        return;
    m_pendingStage = QLatin1String(EffectsSmokeReport::StageCompletion);

    EffectsCompletion::resetCounters();

    // Collect via shared_ptr because callbacks may fire only after this function returns.
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

    // 2. Skipping the animation (the normal path under the NONE profile).
    auto *skipTarget = new SmokeAnimationTarget;
    skipTarget->setParent(this);
    EffectsCompletion::completeNow(skipTarget, [skipped]() { ++(*skipped); });

    // 3. The animation gets destroyed halfway through playback.
    auto *destroyTarget = new SmokeAnimationTarget;
    destroyTarget->setParent(this);
    auto *destroyAnim = new QPropertyAnimation(destroyTarget, "value");
    destroyAnim->setDuration(60000);
    destroyAnim->setEndValue(1.0);
    EffectsCompletion::whenFinished(destroyAnim, destroyTarget,
        [destroyed]() { ++(*destroyed); });
    destroyAnim->start();
    delete destroyAnim;

    // 4. A stuck animation reaped by the watchdog.
    auto *stallTarget = new SmokeAnimationTarget;
    stallTarget->setParent(this);
    auto *stallAnim = new QPropertyAnimation(stallTarget, "value");
    stallAnim->setParent(this);
    stallAnim->setDuration(60000);
    stallAnim->setEndValue(1.0);
    EffectsCompletion::whenFinished(stallAnim, stallTarget, [stalled]() { ++(*stalled); },
        40);
    stallAnim->start();

    // 5. Dead context: no delivery allowed at all.
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
        // Asset stages only count the objects they created themselves.
        G_EFFECTS.resetCounters();
        m_countersBeforeAssets = G_EFFECTS.countersJson();
        scheduleNext(&EffectsSmokeController::stageAnimation);
    });
}

// ── stage: animation ─────────────────────────────────────────────────────────
// PixmapAnimation is the backbone of the lightbox, emotion icons, and judgment
// boxes. Two things must be proven here: with frames it loads; without frames
// it must return nullptr (call sites rely on that nullptr to decide whether to
// tear the lightbox down immediately — that is exactly where the missing-asset
// hang comes from).
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

    // Missing-asset contract: GetPixmapAnimation() must return nullptr. The
    // lightbox, equipment frame, and judge box all rely on that nullptr to
    // decide "finish right now"; if it returned a frame-less item, whatever
    // waits on finished() would wait forever.
    //
    // A real parent (not nullptr) is required to prove anything meaningful:
    // nullptr would return early at the parent guard, making the assertion
    // permanently true while proving nothing.
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

    // Same path in reverse: when the asset exists we must actually get an item,
    // not nullptr every time — that is what lets the assertion above separate
    // "missing asset" from "this function is broken".
    if (fixturesAvailable) {
        PixmapAnimation present;
        present.setPath(frameDir + QLatin1Char('/'));
        details.insert(QStringLiteral("fixture_frame_count"), present.valid());
    }

    emitStage(m_pendingStage, true, details);
    scheduleNext(&EffectsSmokeController::stageGif);
}

// ── stage: gif ───────────────────────────────────────────────────────────────
// Runs the product's EmotionItem (QLabel + QMovie). Four fixtures cover: a
// normal animation, a single frame, a truncated file, and a non-GIF file.
// None of them may crash, and none may leave the label blank.
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
        // After a missing or broken GIF the label must not end up with neither a movie nor any content.
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
// There is no valid synthetic Spine fixture (see tests/fixtures/effects/README.md),
// so this stage verifies lifecycle and degradation: when Spine is disallowed,
// not a single SpineGlItem may be created; when Spine is allowed but the asset
// is missing, broken, or mismatched in letter case, loading must fail and the
// item must be torn down cleanly, without crashing.
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
        // Disallowed means disallowed: this stage never news up its own
        // SpineGlItem to "try it out", because that would no longer be testing
        // the product's behavior.
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
        // Linux is case-sensitive: a path that works on Windows must fail
        // cleanly here, without crashing.
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
        // Broken assets must not pretend to load.
        if (loaded) {
            details.insert(QStringLiteral("cases"), results);
            delete item;
            failStage(m_pendingStage,
                QStringLiteral("Spine case '%1' reported a successful load of a broken asset")
                    .arg(QLatin1String(spineCase.name)), details);
            return;
        }
        // Torn down after the load failure — this delete is the destroy-during-lifecycle verification.
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
// "NONE creates no Spine, QMovie, or video object" is enforced here.
void EffectsSmokeController::stageBudget()
{
    if (m_finished)
        return;
    m_pendingStage = QLatin1String(EffectsSmokeReport::StageBudget);

    QJsonObject counters = G_EFFECTS.countersJson();
    QJsonObject details;
    details.insert(QStringLiteral("counters"), counters);
    details.insert(QStringLiteral("profile"), G_EFFECTS.profileName());

    // Under FULL the Spine stage deliberately creates a few probes to verify degradation; these must not be charged to the budget.
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

    // Deliberately does not delete MainWindow: the product setParent()s Engine
    // to MainWindow, so tearing down the window also tears down the engine,
    // and the normal exit path would then walk into it. This smoke verifies
    // "effect objects are cleaned up", not "the engine can be torn down" — the
    // latter belongs to the M1 startup smoke.
    // MainWindow::closeEvent() is the product's normal exit path and calls
    // qApp->quit() directly. So we only hide() here: the event loop must stay
    // alive until the checks below finish. The real close() runs after
    // finish(), which both exercises the product's own window-close path and
    // still verifies everything.
    if (qApp)
        qApp->setQuitOnLastWindowClosed(false);
    if (m_mainWindow)
        m_mainWindow->hide();

    // Spin the event loop a couple more turns so deleteLater actually lands,
    // then check whether any completion is left dangling. issued !=
    // delivered + cancelled means some flow will never receive its callback —
    // exactly the kind of hang the NONE profile must avoid.
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

        // The verdict is already recorded; now walk the product's own
        // window-close path (closeEvent calls CrashHandler::beginShutdown()
        // then qApp->quit()). Whether we get here or not does not change the
        // exit code — finish() has already fixed it.
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

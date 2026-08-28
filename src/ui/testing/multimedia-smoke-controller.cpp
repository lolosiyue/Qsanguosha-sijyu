#include "multimedia-smoke-controller.h"

#include "audio.h"
#include "audio-backend.h"
#include "engine.h"
#include "homecontroller.h"
#include "mainwindow.h"
#include "settings.h"
#include "ui-startup-smoke-report.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLibraryInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QQuickItem>
#include <QQuickWidget>
#include <QSaveFile>
#include <QTimer>
#include <QVariantMap>
#include <QWindow>

#include <cstdio>
#include <cstdlib>

namespace {

QMutex multimediaSmokeMessageMutex;
QtMessageHandler multimediaSmokePreviousHandler = nullptr;

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

} // namespace

MultimediaSmokeController *MultimediaSmokeController::s_active = nullptr;

void multimediaSmokeMessageHandler(QtMsgType type, const QMessageLogContext &context,
    const QString &message)
{
    {
        QMutexLocker locker(&multimediaSmokeMessageMutex);
        MultimediaSmokeController *controller = MultimediaSmokeController::s_active;
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

    if (multimediaSmokePreviousHandler)
        multimediaSmokePreviousHandler(type, context, message);

    if (type == QtFatalMsg) {
        QMutexLocker locker(&multimediaSmokeMessageMutex);
        MultimediaSmokeController *controller = MultimediaSmokeController::s_active;
        if (controller && !controller->m_finished) {
            controller->m_finished = true;
            writeMarker(MultimediaSmokeReport::resultLine(false,
                controller->m_pendingStage.isEmpty()
                    ? QStringLiteral("backend") : controller->m_pendingStage,
                QStringLiteral("Qt fatal: ") + message,
                MultimediaSmokeReport::SetupFailed));
        }
    }
}

MultimediaSmokeController::MultimediaSmokeController(QObject *parent)
    : QObject(parent)
{
}

MultimediaSmokeController::~MultimediaSmokeController()
{
    QMutexLocker locker(&multimediaSmokeMessageMutex);
    if (s_active == this)
        s_active = nullptr;
}

bool MultimediaSmokeController::isRequested(const QStringList &arguments)
{
    return MultimediaSmokeReport::isRequested(arguments);
}

QString MultimediaSmokeController::fixturePath(const QString &name)
{
    return QDir::current().absoluteFilePath(
        QStringLiteral("tests/fixtures/media/%1").arg(name));
}

bool MultimediaSmokeController::begin(const QStringList &arguments, int *exitCode)
{
    if (exitCode)
        *exitCode = MultimediaSmokeReport::Passed;
    if (!isRequested(arguments))
        return true;

    MultimediaSmokeController *controller = new MultimediaSmokeController(qApp);
    controller->m_arguments = arguments;
    controller->m_pendingStage = QStringLiteral("application");
    controller->m_elapsed.start();
    {
        QMutexLocker locker(&multimediaSmokeMessageMutex);
        s_active = controller;
    }
    multimediaSmokePreviousHandler = qInstallMessageHandler(multimediaSmokeMessageHandler);
    // 任何唔行 finish() 的退出路徑（例如 Engine 建構失敗直接 exit(1)）都要留低
    // 一行 result，CI 唔會見到「marker 缺失」。
    std::atexit(&MultimediaSmokeController::reportUnfinishedAtExit);

    QString error;
    if (!MultimediaSmokeReport::parseTimeoutMs(arguments, &controller->m_timeoutMs, &error)) {
        controller->finish(false, QStringLiteral("arguments"), error,
            MultimediaSmokeReport::InvalidArguments);
        if (exitCode)
            *exitCode = controller->m_exitCode;
        return false;
    }
    controller->m_reportPath = MultimediaSmokeReport::parseReportPath(arguments);
    controller->m_videoSource = MultimediaSmokeReport::parseVideoSource(arguments);

    if (!qobject_cast<QApplication *>(qApp)) {
        controller->finish(false, QStringLiteral("application"),
            QStringLiteral("QApplication was not created (headless QCoreApplication only)"),
            MultimediaSmokeReport::SetupFailed);
        if (exitCode)
            *exitCode = controller->m_exitCode;
        return false;
    }
    return true;
}

int MultimediaSmokeController::abortEarly(const QString &stage, const QString &error,
    int fallbackExitCode)
{
    MultimediaSmokeController *controller = s_active;
    if (!controller)
        return fallbackExitCode;
    controller->finish(false, stage, error,
        MultimediaSmokeReport::exitCodeForFailedStage(stage));
    return controller->m_exitCode;
}

int MultimediaSmokeController::run()
{
    MultimediaSmokeController *controller = s_active;
    if (!controller) {
        writeMarker(MultimediaSmokeReport::resultLine(false, QStringLiteral("backend"),
            QStringLiteral("MultimediaSmokeController::begin() was not called"),
            MultimediaSmokeReport::InternalError));
        return MultimediaSmokeReport::InternalError;
    }
    return controller->execute();
}

QJsonObject MultimediaSmokeController::environmentDetails() const
{
    QJsonObject details;
    details.insert(QStringLiteral("qt_version"), QString::fromLatin1(qVersion()));
    details.insert(QStringLiteral("qt_build_version"), QStringLiteral(QT_VERSION_STR));
    details.insert(QStringLiteral("platform"), qApp ? qApp->platformName() : QString());
    details.insert(QStringLiteral("qt_qpa_platform"), environmentValue("QT_QPA_PLATFORM"));
    details.insert(QStringLiteral("qt_media_backend"), environmentValue("QT_MEDIA_BACKEND"));
    details.insert(QStringLiteral("qt_quick_backend"), environmentValue("QT_QUICK_BACKEND"));
    details.insert(QStringLiteral("display"), environmentValue("DISPLAY"));
    details.insert(QStringLiteral("qml_import_path"),
        QLibraryInfo::path(QLibraryInfo::QmlImportsPath));
    details.insert(QStringLiteral("plugin_path"),
        QLibraryInfo::path(QLibraryInfo::PluginsPath));
    details.insert(QStringLiteral("configured_backend"),
        QStringLiteral(QSAN_AUDIO_BACKEND_NAME));
    details.insert(QStringLiteral("forced_video_source"), m_videoSource);
    details.insert(QStringLiteral("working_directory"), QDir::currentPath());
    details.insert(QStringLiteral("timeout_ms"), m_timeoutMs);
    return details;
}

QJsonObject MultimediaSmokeController::audioDiagnostics() const
{
    return Audio::diagnostics();
}

int MultimediaSmokeController::execute()
{
    m_pendingStage = QStringLiteral("engine");
    if (failIfDeadlineExceeded(QStringLiteral("engine")))
        return m_exitCode;
    if (!Sanguosha) {
        finish(false, QStringLiteral("engine"), QStringLiteral("Engine instance is null"),
            MultimediaSmokeReport::SetupFailed);
        return m_exitCode;
    }

    if (!m_videoSource.isEmpty()) {
        // 只改記憶體入面的 Config，唔寫返落使用者的設定檔：smoke 唔應該留低
        // 一個壞背景畀下次正常啟動。
        Config.BackgroundImage = m_videoSource;
    }

    m_pendingStage = QStringLiteral("main_window");
    MainWindow *window = new MainWindow;
    m_mainWindow = window;
    Sanguosha->setParent(window);
    window->show();

    if (failIfDeadlineExceeded(QStringLiteral("main_window")))
        return m_exitCode;
    if (!window->isVisible() || !window->windowHandle()) {
        finish(false, QStringLiteral("main_window"),
            QStringLiteral("MainWindow did not obtain a native window handle"),
            MultimediaSmokeReport::SetupFailed);
        return m_exitCode;
    }

    m_pendingStage = QStringLiteral("home_scene");
    connect(window, &MainWindow::homeSceneReady,
        this, &MultimediaSmokeController::onHomeSceneReady);
    connect(window, &MainWindow::homeSceneFailed,
        this, &MultimediaSmokeController::onHomeSceneFailed);

    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    m_timeoutTimer->setInterval(qMax(1, remainingMs()));
    connect(m_timeoutTimer, &QTimer::timeout, this, &MultimediaSmokeController::onTimeout);
    m_timeoutTimer->start();

    QTimer::singleShot(0, this, &MultimediaSmokeController::onEventLoopEntered);

    const int rc = qApp->exec();
    if (!m_finished) {
        finish(false, m_pendingStage,
            QStringLiteral("event loop exited before the multimedia smoke completed"),
            MultimediaSmokeReport::SetupFailed);
    }
    return m_exitCode != MultimediaSmokeReport::Passed ? m_exitCode : rc;
}

void MultimediaSmokeController::onEventLoopEntered()
{
    if (m_finished)
        return;
    m_eventLoopEntered = true;
    if (!m_mainWindow) {
        finish(false, QStringLiteral("main_window"),
            QStringLiteral("MainWindow was destroyed during startup"),
            MultimediaSmokeReport::SetupFailed);
        return;
    }
    // setSource() 對 qrc 係同步的，signal 好可能喺 MainWindow 建構期間已經發出。
    if (m_mainWindow->isHomeSceneReady())
        onHomeSceneReady();
    else if (m_mainWindow->hasHomeSceneError())
        onHomeSceneFailed(m_mainWindow->homeSceneError());
}

void MultimediaSmokeController::onHomeSceneReady()
{
    if (m_finished || m_homeSceneReady)
        return;
    m_homeSceneReady = true;
    scheduleNext(&MultimediaSmokeController::stageBackend, 0);
}

void MultimediaSmokeController::onHomeSceneFailed(const QString &error)
{
    if (m_finished)
        return;
    finish(false, QStringLiteral("home_scene"), error, MultimediaSmokeReport::SetupFailed);
}

void MultimediaSmokeController::scheduleNext(
    void (MultimediaSmokeController::*slot)(), int delayMs)
{
    if (m_finished)
        return;
    QTimer::singleShot(delayMs, this, slot);
}

// ── stage: backend ───────────────────────────────────────────────────────────
void MultimediaSmokeController::stageBackend()
{
    if (m_finished)
        return;
    m_pendingStage = QStringLiteral("backend");

    Audio::init();
    QJsonObject details = audioDiagnostics();
    details.insert(QStringLiteral("configured_backend"),
        QStringLiteral(QSAN_AUDIO_BACKEND_NAME));
    details.insert(QStringLiteral("output_device"), Audio::hasOutputDevice());

    if (!Audio::isInitialized() || Audio::backendName() == QLatin1String("none")) {
        failStage(QStringLiteral("backend"),
            QStringLiteral("Audio facade has no backend after init()"), details);
        return;
    }
    emitStage(QStringLiteral("backend"), true, details);
    scheduleNext(&MultimediaSmokeController::stageUiEffect);
}

// ── stage: 短 UI 音效 ────────────────────────────────────────────────────────
void MultimediaSmokeController::stageUiEffect()
{
    if (m_finished || failIfDeadlineExceeded(QStringLiteral("ui_effect")))
        return;
    m_pendingStage = QStringLiteral("ui_effect");

    // fixture 的檔名故意用 button-down：classifyAudioFile() 會將佢當短 UI 音效，
    // 即係真係行緊 QSoundEffect 嗰條路，而唔係語音 pool。
    const QString effect = fixturePath(QStringLiteral("button-down.wav"));
    const bool available = QFileInfo::exists(effect);
    QJsonObject details;
    details.insert(QStringLiteral("fixture"), effect);
    details.insert(QStringLiteral("fixture_available"), available);
    details.insert(QStringLiteral("classified_as_effect"),
        classifyAudioFile(effect) == AudioChannel::Effect);

    if (available) {
        Audio::play(effect, false);
        // superpose=false：第二次唔應該疊住播。呢度驗嘅係唔會 crash 同唔會
        // 每次都開新資源，實際有冇聲 CI 上驗唔到。
        Audio::play(effect, false);
        Audio::play(effect, true);
    }
    details.insert(QStringLiteral("audio"), audioDiagnostics());
    emitStage(QStringLiteral("ui_effect"), true, details);
    scheduleNext(&MultimediaSmokeController::stageVoice);
}

// ── stage: 武將語音 pool ─────────────────────────────────────────────────────
void MultimediaSmokeController::stageVoice()
{
    if (m_finished || failIfDeadlineExceeded(QStringLiteral("voice")))
        return;
    m_pendingStage = QStringLiteral("voice");

    const QString voice = fixturePath(QStringLiteral("voice-line.wav"));
    const bool available = QFileInfo::exists(voice);
    QJsonObject details;
    details.insert(QStringLiteral("fixture"), voice);
    details.insert(QStringLiteral("fixture_available"), available);
    details.insert(QStringLiteral("classified_as_voice"),
        classifyAudioFile(voice) == AudioChannel::Voice);

    int requested = 0;
    if (available) {
        // 特登超出 pool 上限：要證明 player 係被回收／搶佔，而唔係每次 new 一對
        // player+output 落去 leak。
        for (int i = 0; i < 12; ++i) {
            Audio::play(voice, true);
            ++requested;
        }
        Audio::play(voice, false);
        ++requested;
    }
    details.insert(QStringLiteral("play_requests"), requested);
    details.insert(QStringLiteral("audio"), audioDiagnostics());
    emitStage(QStringLiteral("voice"), true, details);
    scheduleNext(&MultimediaSmokeController::stageBgm);
}

// ── stage: BGM ───────────────────────────────────────────────────────────────
void MultimediaSmokeController::stageBgm()
{
    if (m_finished || failIfDeadlineExceeded(QStringLiteral("bgm")))
        return;
    m_pendingStage = QStringLiteral("bgm");

    const QString bgm = fixturePath(QStringLiteral("bgm-loop.wav"));
    const bool available = QFileInfo::exists(bgm);
    QJsonObject details;
    details.insert(QStringLiteral("fixture"), bgm);
    details.insert(QStringLiteral("fixture_available"), available);

    if (available) {
        Audio::playBGM(bgm);
        Audio::setBGMVolume(0.5f);
        // 同一個來源再 call 一次唔應該重新開始，亦唔應該多開一個 player。
        Audio::playBGM(bgm);
    }
    details.insert(QStringLiteral("audio"), audioDiagnostics());
    emitStage(QStringLiteral("bgm"), true, details);
    scheduleNext(&MultimediaSmokeController::stageMissingAsset);
}

// ── stage: 缺檔案降級 ────────────────────────────────────────────────────────
void MultimediaSmokeController::stageMissingAsset()
{
    if (m_finished || failIfDeadlineExceeded(QStringLiteral("missing_asset")))
        return;
    m_pendingStage = QStringLiteral("missing_asset");

    const QString missing = fixturePath(QStringLiteral("does-not-exist.ogg"));
    QJsonObject details;
    details.insert(QStringLiteral("missing_path"), missing);
    details.insert(QStringLiteral("missing_confirmed"), !QFileInfo::exists(missing));

    // 呢三個 call 全部應該只係 warning。行到下一行就證明冇 crash。
    Audio::play(missing, false);
    Audio::play(missing, true);
    Audio::playBGM(missing);

    details.insert(QStringLiteral("audio"), audioDiagnostics());
    emitStage(QStringLiteral("missing_asset"), true, details);
    scheduleNext(&MultimediaSmokeController::stageVideo);
}

// ── stage: 影片／QML media component ─────────────────────────────────────────
void MultimediaSmokeController::stageVideo()
{
    if (m_finished || failIfDeadlineExceeded(QStringLiteral("video")))
        return;
    m_pendingStage = QStringLiteral("video");

    HomeController *home = m_mainWindow ? m_mainWindow->homeSceneController() : nullptr;
    if (!home) {
        failStage(QStringLiteral("video"),
            QStringLiteral("HomeController is not available; HomeScene did not initialise"));
        return;
    }

    const QVariantMap status = home->videoStatus();
    const QString reason = status.value(QStringLiteral("reason")).toString();
    const QString error = status.value(QStringLiteral("error")).toString();
    const bool loaded = status.value(QStringLiteral("loaded")).toBool();
    const bool fallback = status.value(QStringLiteral("fallback")).toBool();

    m_videoResult = MultimediaSmokeReport::videoPayload(home->hasVideoSupport(), loaded,
        fallback, reason, error);
    m_videoResult.insert(QStringLiteral("enabled"), home->videoBackgroundEnabled());
    m_videoResult.insert(QStringLiteral("fallback_confirmed"),
        status.value(QStringLiteral("fallback_confirmed")).toBool());
    writeMarker(MultimediaSmokeReport::videoLine(m_videoResult));

    QJsonObject details = m_videoResult;
    details.insert(QStringLiteral("home_scene_ready"), m_homeSceneReady);
    details.insert(QStringLiteral("forced_video_source"), m_videoSource);
    details.insert(QStringLiteral("backdrop"), home->backgroundImage().toString());

    if (reason.isEmpty()) {
        // 冇人報告過 = 分辨唔到成敗。呢個先至係真正的失敗。
        failStage(QStringLiteral("video"),
            QStringLiteral("HomeScene never reported a video background status"), details);
        return;
    }
    if (!MultimediaSmokeReport::isAcceptableVideoReason(reason)) {
        failStage(QStringLiteral("video"),
            QStringLiteral("unclassified video background reason '%1'").arg(reason), details);
        return;
    }
    // 播唔到唔係失敗，只要靜態背景頂得住而且 HomeScene 仲喺度。
    QQuickWidget *view = m_mainWindow ? m_mainWindow->homeSceneView() : nullptr;
    if (!view || !view->rootObject()) {
        failStage(QStringLiteral("video"),
            QStringLiteral("HomeScene root object did not survive the video stage"), details);
        return;
    }
    emitStage(QStringLiteral("video"), true, details);
    scheduleNext(&MultimediaSmokeController::stageShutdown);
}

// ── stage: shutdown ──────────────────────────────────────────────────────────
void MultimediaSmokeController::stageShutdown()
{
    if (m_finished || failIfDeadlineExceeded(QStringLiteral("shutdown")))
        return;
    m_pendingStage = QStringLiteral("shutdown");

    QJsonObject details;
    details.insert(QStringLiteral("audio_before"), audioDiagnostics());

    Audio::stop();
    Audio::quit();

    details.insert(QStringLiteral("backend_after_quit"), Audio::backendName());
    details.insert(QStringLiteral("initialized_after_quit"), Audio::isInitialized());
    details.insert(QStringLiteral("elapsed_ms"), static_cast<int>(m_elapsed.elapsed()));

    if (Audio::isInitialized()) {
        failStage(QStringLiteral("shutdown"),
            QStringLiteral("Audio::quit() left the facade initialised"), details);
        return;
    }
    // quit() 之後再 call 唔應該 crash：關機途中仲有可能收到 audio 請求。
    Audio::play(fixturePath(QStringLiteral("voice-line.wav")), false);
    Audio::stopBGM();

    emitStage(QStringLiteral("shutdown"), true, details);
    finish(true, QStringLiteral("shutdown"), QString(), MultimediaSmokeReport::Passed);
}

void MultimediaSmokeController::onTimeout()
{
    if (m_finished)
        return;
    failIfDeadlineExceeded(m_pendingStage.isEmpty()
        ? QStringLiteral("backend") : m_pendingStage, true);
}

int MultimediaSmokeController::remainingMs() const
{
    return m_timeoutMs - static_cast<int>(m_elapsed.elapsed());
}

bool MultimediaSmokeController::failIfDeadlineExceeded(const QString &stage, bool force)
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
        QStringLiteral("multimedia smoke timed out after %1 ms (limit %2 ms) while waiting "
                       "for stage '%3'").arg(elapsed).arg(m_timeoutMs).arg(stage),
        MultimediaSmokeReport::Timeout);
    return true;
}

void MultimediaSmokeController::reportUnfinishedAtExit()
{
    MultimediaSmokeController *controller = s_active;
    if (!controller || controller->m_finished)
        return;
    controller->m_finished = true;
    const QString stage = controller->m_pendingStage.isEmpty()
        ? QStringLiteral("backend") : controller->m_pendingStage;
    writeMarker(MultimediaSmokeReport::resultLine(false, stage,
        QStringLiteral("process exited during stage '%1' without completing the multimedia "
                       "smoke").arg(stage),
        MultimediaSmokeReport::exitCodeForFailedStage(stage)));
}

void MultimediaSmokeController::emitStage(const QString &stage, bool ok,
    const QJsonObject &details)
{
    m_stages.append(MultimediaSmokeReport::stagePayload(stage, ok, details));
    writeMarker(MultimediaSmokeReport::stageLine(stage, ok, details));
}

void MultimediaSmokeController::failStage(const QString &stage, const QString &error,
    const QJsonObject &details)
{
    QJsonObject payload = details;
    payload.insert(QStringLiteral("error"), error);
    emitStage(stage, false, payload);
    finish(false, stage, error, MultimediaSmokeReport::exitCodeForFailedStage(stage));
}

void MultimediaSmokeController::finish(bool ok, const QString &stage, const QString &error,
    MultimediaSmokeReport::ExitCode exitCode)
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
    details.insert(QStringLiteral("audio"), audioDiagnostics());
    if (!m_videoResult.isEmpty())
        details.insert(QStringLiteral("video"), m_videoResult);

    writeMarker(MultimediaSmokeReport::resultLine(ok, stage, error, exitCode, details));
    writeReportFile();

    if (qApp && m_eventLoopEntered)
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}

void MultimediaSmokeController::writeReportFile()
{
    if (m_reportPath.isEmpty())
        return;
    QJsonObject report;
    report.insert(QStringLiteral("schema_version"), MultimediaSmokeReport::schemaVersion());
    report.insert(QStringLiteral("ok"), m_exitCode == MultimediaSmokeReport::Passed);
    report.insert(QStringLiteral("exit_code"), m_exitCode);
    report.insert(QStringLiteral("stages"), m_stages);
    report.insert(QStringLiteral("environment"), environmentDetails());
    report.insert(QStringLiteral("audio"), audioDiagnostics());
    report.insert(QStringLiteral("video"), m_videoResult);
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

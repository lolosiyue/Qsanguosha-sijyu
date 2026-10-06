#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <QTimer>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QLoggingCategory>
#include <QApplication>
#include <QPixmapCache>
#include <QCoreApplication>
#include <QStringList>
#include <QScopeGuard>
#if defined(Q_OS_WIN) && !defined(QSAN_XP_LEGACY)
#include <windows.h>
#endif

#include "mainwindow.h"
#if !defined(QSAN_XP_LEGACY)
#include "widget-accessibility.h"
#endif
#include "settings.h"
#include "banpair.h"
#include "server.h"
#include "engine.h"
#include "engine-bootstrap.h"
#include "audio.h"
#include <QSurfaceFormat>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QSGRendererInterface>

#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

#include "asset-manifest.h"
#include "runtime-paths.h"
#include "startup-timing.h"
#include "interaction-descriptor-registry.h"

#include "crashhandler.h"
#include "effects/effects-policy.h"
#include "effects/effects-profile.h"
#include "testing/effects-smoke-controller.h"
#include "testing/local-response-ui-controller.h"
#include "testing/multimedia-smoke-controller.h"
#include "testing/network-ui-smoke-controller.h"
#include "testing/ui-startup-smoke-controller.h"
#include "websocket-gateway.h"
#ifdef Q_OS_ANDROID
#include "android-content-store.h"
#include "android-content-dialog.h"
#include "android-dialog-fit.h"
#include <QMessageBox>
#endif

// Eats QEvent::ToolTip (and context-help tooltip requests) at application
// scope so nothing in big-picture mode can pop hover help. Installed only
// while the mode is active; normal runs are untouched.
class BigPictureTooltipFilter : public QObject
{
public:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::ToolTip
            || event->type() == QEvent::WhatsThis)
            return true;
        return QObject::eventFilter(watched, event);
    }
};

// Best-effort primary-screen pixel height probed before QApplication exists;
// returns 0 when the screen cannot be measured (no display server reachable).
static int probeScreenPixelHeight()
{
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    // xdpyinfo reports the X screen's raw pixel dimensions; under XWayland it
    // reports the compositor size, still the right "logical height" here.
    // Only runs in big-picture mode, so the fork cost is paid once per launch.
    if (FILE *fp = popen("xdpyinfo 2>/dev/null", "r")) {
        char line[256];
        int height = 0;
        while (fgets(line, sizeof(line), fp)) {
            const char *dim = strstr(line, "dimensions:");
            if (dim) {
                unsigned w = 0, h = 0;
                if (sscanf(dim + 11, "%ux%u", &w, &h) == 2)
                    height = static_cast<int>(h);
                break;
            }
        }
        pclose(fp);
        return height;
    }
    return 0;
#elif defined(Q_OS_WIN) && !defined(QSAN_XP_LEGACY)
    return GetSystemMetrics(SM_CYSCREEN);
#else
    return 0;
#endif
}

// Estimate what QScreen::devicePixelRatio() will report before the platform
// plugin exists. A value > 1.0 means the platform already scales and
// QT_SCALE_FACTOR must not be forced on top.
static qreal estimateScreenDpr()
{
    // Explicit user/app scale settings always win and already settle the DPR.
    if (qEnvironmentVariableIsSet("QT_SCALE_FACTOR")
        || qEnvironmentVariableIsSet("QT_SCREEN_SCALE_FACTORS"))
        return 2.0;
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    // Qt on X11/XWayland reports DPR 1 for unscaled sessions; Wayland-owned
    // fractional scaling reaches Qt through the env vars checked above.
    return 1.0;
#elif defined(Q_OS_WIN) && !defined(QSAN_XP_LEGACY)
    using GetDpiForSystem_t = UINT(WINAPI *)();
    static const GetDpiForSystem_t getDpiForSystem = reinterpret_cast<GetDpiForSystem_t>(
        reinterpret_cast<void *>(GetProcAddress(
            GetModuleHandleW(L"user32.dll"), "GetDpiForSystem")));
    if (getDpiForSystem)
        return getDpiForSystem() / 96.0;
    return 1.0;
#else
    return 1.0;
#endif
}

// REPORT §3.1: a 4K panel without OS scaling (logical height > 1440, DPR 1 —
// typical on Linux/X11 and Windows at 100 %) leaves QWidget dialogs and the
// table unreadably small. Force QT_SCALE_FACTOR=2 so the session runs as
// 1080p logical with DPR 2. Must run before QApplication is constructed.
static void applyBigPictureScaleFactor()
{
    if (qgetenv("QT_ENABLE_HIGHDPI_SCALING") == "0")
        return; // High-DPI disabled outright; a forced factor is ignored anyway.
    if (estimateScreenDpr() > 1.0)
        return;
    if (probeScreenPixelHeight() > 1440)
        qputenv("QT_SCALE_FACTOR", "2");
}

int main(int argc, char *argv[]) {
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    // Under WSLg/XWayland, XI2 sends clicks to the window frame but not the QQuickWidget/QOpenGLWidget client area.
    // Use libc setenv so the platform plugin sees the setting before Qt initializes.
    setenv("QT_XCB_NO_XI2", "1", 1);
#endif
    QSanStartupTiming startupTotal("main.before_window");
    QSanStartupTiming startupPhase("main.application");
    CrashHandler::install();
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--local-response-ui-capabilities") == 0) {
            fputs("{\"schema_version\":1,\"auto\":true,\"show\":true,\"inspect\":true}\n", stdout);
            fflush(stdout);
            return 0;
        }
        if (strcmp(argv[i], "--interaction-inventory") == 0) {
            const QByteArray json = QJsonDocument(
                InteractionDescriptorRegistry::inventoryDocument())
                .toJson(QJsonDocument::Indented);
            if (i + 1 < argc) {
                QFile output(QString::fromLocal8Bit(argv[i + 1]));
                if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    fprintf(stderr, "cannot write interaction inventory: %s\n",
                        qPrintable(output.errorString()));
                    return 2;
                }
                if (output.write(json) != json.size())
                    return 3;
            } else {
                fwrite(json.constData(), 1, static_cast<size_t>(json.size()), stdout);
                fflush(stdout);
            }
            return 0;
        }
    }
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--seed") == 0) {
            qputenv("QT_HASH_SEED", "0");
            break;
        }
    }

    // Big-picture (10-foot) mode: --big-picture, the persisted BigPicture/Enabled
    // key, or a preset QSAN_BIG_PICTURE env var. Normalize the first two into the
    // env var now so every later stage only checks qsanBigPictureModeActive().
    bool bigPictureMode = false;
    {
        const QByteArray bigPictureEnv = qgetenv("QSAN_BIG_PICTURE");
        if (!bigPictureEnv.isEmpty())
            bigPictureMode = bigPictureEnv != "0"
                && bigPictureEnv.compare("false", Qt::CaseInsensitive) != 0;
        for (int i = 1; i < argc && !bigPictureMode; ++i) {
            if (strcmp(argv[i], "--big-picture") == 0)
                bigPictureMode = true;
        }
        if (!bigPictureMode)
            bigPictureMode = Config.value(QStringLiteral("BigPicture/Enabled"), false).toBool();
        if (bigPictureMode)
            qputenv("QSAN_BIG_PICTURE", "1");
    }

    if (argc > 2 && strcmp(argv[1], "-crashtest") == 0) {
        new QCoreApplication(argc, argv);
        CrashHandler::selfTest(argv[2]);
        return 0;
    }


    // Qt 6 is High-DPI aware by default. Preserve fractional per-screen scale
    // factors so moving the window between monitors does not snap the UI size.
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setAlphaBufferSize(8);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    QSurfaceFormat::setDefaultFormat(format);

#ifdef Q_OS_ANDROID
    // Android's translated GLES context can be lost during Activity recreation.
    // Pair software Qt Quick with the existing raster FitView viewport.
    QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
    qInfo("Android rendering: Qt Quick software, raster viewport");
#else
    // QOpenGLWidget and QQuickWidget must use the same graphics API when
    // they are composed in the same top-level window.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
#endif

    // Headless mode uses QCoreApplication only, avoiding GUI plugins and enabling display-free startup.
    QStringList appArgs;
    for (int i = 1; i < argc; ++i)
        appArgs << QString::fromLocal8Bit(argv[i]);

    bool hasTestScenarioArg = appArgs.contains("--test-scenario");
    foreach (const QString &arg, appArgs) {
        if (arg.startsWith("--test-scenario=")) {
            hasTestScenarioArg = true;
            break;
        }
    }

    // --ui-startup-smoke is the real GUI startup verification; it must run the QApplication
    // path and must not be intercepted by the headless check.
    const bool uiStartupSmoke = UiStartupSmokeController::isRequested(appArgs);
    const auto exitStartupSmoke = [](int code) -> void {
        CrashHandler::beginShutdown();
        fflush(nullptr);
        std::_Exit(code);
    };
    // --multimedia-smoke needs the full GUI path just like the startup smoke: the audio
    // backend and QML media components only exist under QApplication.
    const bool multimediaSmoke = MultimediaSmokeController::isRequested(appArgs);
    // --effects-smoke needs the full GUI path like the two above: the effect classes
    // RoomScene uses (QMovie/SpineGlItem/PixmapAnimation) only exist under QApplication.
    const bool effectsSmoke = EffectsSmokeController::isRequested(appArgs);

    // --asset-report is pure diagnostic output and must not require a display: like -server
    // it runs the QCoreApplication path.
    const bool headlessApp = !uiStartupSmoke && !multimediaSmoke && !effectsSmoke
        && (appArgs.contains("-server")
            || appArgs.contains("--headless")
            || appArgs.contains("--asset-report")
            || (hasTestScenarioArg && appArgs.contains("-h")));

    // Reject unknown profile names so CI cannot silently run the default profile.
    {
        const EffectsProfileContract::CliOverride effectsCli =
            EffectsProfileContract::parseCliOverride(appArgs);
        if (effectsCli.present && !effectsCli.valid) {
            fprintf(stderr, "%s\n", qPrintable(effectsCli.error));
            return 5;
        }
    }

    if (argc > 1 && strcmp(argv[1], "-manual") == 0) {
        new QCoreApplication(argc, argv);
#ifdef Q_OS_ANDROID
        QString pathError;
        if (!QSanRuntimePaths::resolve(qApp->arguments(), &pathError)) {
            fprintf(stderr, "%s\n", qPrintable(pathError));
            return 6;
        }
#endif
        if (!EngineBootstrap::initialize(true))
            return 1;
        return 0;
    } else if (headlessApp)
        new QCoreApplication(argc, argv);
    else {
        if (bigPictureMode)
            applyBigPictureScaleFactor();
        new QApplication(argc, argv);
        // Qt's 10 MB default evicts decoded skin and emotion frames, forcing slow PNG decoding on the GUI thread.
#ifdef Q_OS_ANDROID
        QPixmapCache::setCacheLimit(128 * 1024);
#else
        QPixmapCache::setCacheLimit(256 * 1024);
#endif
#if !defined(QSAN_XP_LEGACY)
        installWidgetAccessibility(QCoreApplication::instance());
#endif
        // The home page uses custom contentItem and indicator controls unsupported by the Windows native style.
        QQuickStyle::setStyle(QStringLiteral("Basic"));

        if (bigPictureMode) {
            // Hover tooltips are a desktop concept; suppress them on TV.
            static BigPictureTooltipFilter tooltipFilter;
            qApp->installEventFilter(&tooltipFilter);
        }
    }

#ifdef Q_OS_ANDROID
    AndroidContentStore androidContent;
#endif
    const auto releaseEffectsApplication = qScopeGuard([effectsSmoke]() {
        if (effectsSmoke) {
            // The smoke releases its window/controller first. QApplication
            // must then release shared GL/thread resources before Qt statics,
            // including on argument, initialization and timeout failures.
            CrashHandler::beginShutdown();
            delete QCoreApplication::instance();
        }
    });

#ifdef Q_OS_ANDROID
    if (!headlessApp) {
        installAndroidDialogFit(qobject_cast<QApplication *>(QCoreApplication::instance()));
        // Content dialogs run before the runtime tree exists; use the catalogs embedded in the APK.
        // Both translators uninstall themselves when this block ends.
        QTranslator startupQtTranslator, startupTranslator;
        if (startupQtTranslator.load(QStringLiteral(":/assets/translations/qt_zh_CN.qm")))
            qApp->installTranslator(&startupQtTranslator);
        if (startupTranslator.load(QStringLiteral(":/assets/translations/sanguosha.qm")))
            qApp->installTranslator(&startupTranslator);
        QString contentError;
        AndroidContentDialog::configure(&androidContent);
        if (!AndroidContentDialog::prepareStartup(&contentError)) {
            QMessageBox::critical(nullptr, AndroidContentDialog::tr("Resource initialization failed"), contentError);
            return 6;
        }
        // Keep all game creation behind content recovery; media is optional.
        if (!AndroidContentDialog::prepareForHome()) return 0;
        qApp->setProperty("androidRuntimeRoot", androidContent.runtimeRoot());
    }
#endif

    startupPhase.next("main.runtime_paths");
    // Resolve and select the runtime asset root before any smoke controller, engine, or asset access.
    {
        QString pathError;
        if (!QSanRuntimePaths::resolve(qApp->arguments(), &pathError)) {
            fprintf(stderr, "%s\n", qPrintable(pathError));
            for (const QString &line : QSanRuntimePaths::resolution().candidates)
                fprintf(stderr, "  tried %s\n", qPrintable(line));
            Server::writeHeadlessLog("ERROR: " + pathError);
            if (uiStartupSmoke)
                exitStartupSmoke(UiStartupSmokeController::abortEarly(
                    QStringLiteral("runtime_paths"), pathError, 6));
            if (multimediaSmoke)
                return MultimediaSmokeController::abortEarly(
                    QStringLiteral("runtime_paths"), pathError, 6);
            if (effectsSmoke)
                return EffectsSmokeController::abortEarly(
                    QStringLiteral("runtime_paths"), pathError, 6);
            return 6;
        }
    }

    startupPhase.next("main.arguments_plugins_translations");
    // Print asset resolution and manifest state without starting a game.
    if (appArgs.contains(QStringLiteral("--asset-report"))) {
        QVariantMap payload;
        payload.insert(QStringLiteral("schema_version"), 1);
        payload.insert(QStringLiteral("runtime_paths"), QSanRuntimePaths::describe());
        // The build-tree manifest lists expected missing assets; CMake generates it in the build directory.
        QString manifestOverride;
        const int manifestIndex = appArgs.indexOf(QStringLiteral("--asset-manifest"));
        if (manifestIndex >= 0 && manifestIndex + 1 < appArgs.size())
            manifestOverride = appArgs.at(manifestIndex + 1);
        const QSanAssetManifest::Report assetReport =
            QSanAssetManifest::inspect(QString(), manifestOverride);
        payload.insert(QStringLiteral("assets"), QSanAssetManifest::describe(assetReport));
        const QByteArray json = QJsonDocument(QJsonObject::fromVariantMap(payload))
                                    .toJson(QJsonDocument::Indented);
        fwrite(json.constData(), 1, json.size(), stdout);
        for (const QString &line : QSanAssetManifest::diagnostics(assetReport))
            fprintf(stderr, "%s\n", qPrintable(line));
        fflush(nullptr);
        return assetReport.complete() ? 0 : 7;
    }

    // The GUI startup smoke runs after QApplication exists and records the application stage.
    if (uiStartupSmoke) {
        int smokeExitCode = 0;
        if (!UiStartupSmokeController::begin(qApp->arguments(), &smokeExitCode))
            exitStartupSmoke(smokeExitCode);
    }
    if (multimediaSmoke) {
        int smokeExitCode = 0;
        if (!MultimediaSmokeController::begin(qApp->arguments(), &smokeExitCode))
            return smokeExitCode;
    }
    if (effectsSmoke) {
        int smokeExitCode = 0;
        if (!EffectsSmokeController::begin(qApp->arguments(), &smokeExitCode))
            return smokeExitCode;
    }

    // Artwork PNGs contain an invalid sRGB iCCP profile that libpng 1.6+ warns about for each image.
    // Suppress this harmless color-profile warning to keep the console readable.
    QLoggingCategory::setFilterRules(QStringLiteral("qt.gui.imageio.warning=false"));

#ifdef Q_OS_WIN
    qputenv("QT_MEDIA_BACKEND", "ffmpeg");
#endif

    // Parse --headless-log early so startup errors can be recorded even when GUI stdout is unavailable.
    const int earlyLogIdx = qApp->arguments().indexOf("--headless-log");
    if (earlyLogIdx >= 0 && earlyLogIdx + 1 < qApp->arguments().size())
        Server::setHeadlessLogFile(qApp->arguments().at(earlyLogIdx + 1));

    qsanSeedRandom(QTime(0, 0, 0).secsTo(QTime::currentTime()));

    const QStringList arguments = qApp->arguments();
    const int seedIdx = arguments.indexOf("--seed");
    if (seedIdx >= 0) {
        const QString candidate = seedIdx + 1 < arguments.size() ? arguments.at(seedIdx + 1) : QString();
        const QString seedText = candidate.startsWith("--") ? QString() : candidate;
        QString seedError;
        if (!Server::configureGameSeed(seedText, &seedError)) {
            if (headlessApp)
                Server::writeHeadlessLog("ERROR: " + seedError);
            else
                qCritical().noquote() << seedError;
            return 1;
        }
    }

    QCoreApplication::addLibraryPath(QCoreApplication::applicationDirPath() + "/plugins");

#ifdef Q_OS_WIN
    // Qt may relocate its prefix to the executable directory when Qt DLLs sit beside the executable.
    // Add the Qt installation plugin directory so the multimedia backend remains discoverable.
    // This Windows-only DLL-search fix is unnecessary with distro Qt on Linux.
    const QString qtBinDir = QStringLiteral(QT_BIN_DIR);
    QCoreApplication::addLibraryPath(QDir(qtBinDir).filePath("../plugins"));
    QString path = qEnvironmentVariable("PATH");
    if (!path.contains(qtBinDir, Qt::CaseInsensitive)) {
        if (!path.isEmpty())
            path.prepend(QDir::listSeparator());
        path.prepend(qtBinDir);
        qputenv("PATH", path.toUtf8());
    }
#endif

    // Load translations from the install tree or the legacy asset root to avoid silently falling back to English.
    QTranslator qt_translator, translator;
    const auto loadTranslation = [](QTranslator &target, const QString &fileName) {
#ifdef Q_OS_ANDROID
        // The runtime copy is deployed once and never refreshed, and extensions cannot ship
        // catalogs; the APK's embedded catalog always matches this binary.
        if (target.load(QStringLiteral(":/assets/translations/") + fileName))
            return;
#endif
        if (target.load(QSanRuntimePaths::assetPath(QStringLiteral("translations/") + fileName)))
            return;
        target.load(QSanRuntimePaths::assetPath(fileName));
    };
    loadTranslation(qt_translator, QStringLiteral("qt_zh_CN.qm"));
    loadTranslation(translator, QStringLiteral("sanguosha.qm"));

    qApp->installTranslator(&qt_translator);
    qApp->installTranslator(&translator);

#ifdef Q_OS_ANDROID
    if (!headlessApp) {
        QString contentError;
        if (!androidContent.beginBootAttempt(&contentError)) {
            QMessageBox::critical(nullptr, AndroidContentDialog::tr("Cannot record startup state"), contentError);
            return 6;
        }
    }
#endif
    startupPhase.next("main.engine");
    if (!EngineBootstrap::initialize()) {
        Server::writeHeadlessLog("ERROR: EngineBootstrap::initialize failed");
        if (uiStartupSmoke)
            exitStartupSmoke(UiStartupSmokeController::abortEarly(QStringLiteral("engine"),
                QStringLiteral("EngineBootstrap::initialize failed"), 1));
        if (multimediaSmoke)
            return MultimediaSmokeController::abortEarly(QStringLiteral("engine"),
                QStringLiteral("EngineBootstrap::initialize failed"), 1);
        if (effectsSmoke)
            return EffectsSmokeController::abortEarly(QStringLiteral("engine"),
                QStringLiteral("EngineBootstrap::initialize failed"), 1);
        return 1;
    }
    startupPhase.next("main.engine_connections");
    // Do not destroy Engine from aboutToQuit: deferred deletion can invalidate the global Sanguosha pointer during teardown.
    QObject::disconnect(qApp, SIGNAL(aboutToQuit()), Sanguosha, SLOT(deleteLater()));
    // Register the actual version with the crash handler after Engine initialization.
    CrashHandler::setVersion(Sanguosha->getVersionNumber().toUtf8().constData());
#ifdef AUDIO_SUPPORT
    QObject::connect(Sanguosha, &Engine::audioEffectRequested,
                     [](const QString &filename, bool superpose) { Audio::play(filename, superpose); });
#endif
    startupPhase.next("main.settings");
    Config.init();
    // Resolve the effects profile before creating UI objects that consult the policy during construction.
    startupPhase.next("main.effects_settings");
    G_EFFECTS.initialize(qApp->arguments());
    // Load UiConfig fonts only with QGuiApplication; headless mode does not need fonts or a palette.
    startupPhase.next("main.ui_fonts");
    if (qobject_cast<QApplication *>(qApp))
        UiConfig.init();
    startupPhase.next("main.theme_font_apply");
    applyColorScheme(Config.ColorScheme);
    applyVisualMode(Config.VisualMode);
    if (qobject_cast<QApplication *>(qApp))
        qApp->setFont(UiConfig.AppFont);
    startupPhase.next("main.banpairs");
    BanPair::loadBanPairs();
    startupPhase.next("main.mode_dispatch");
#if QSAN_ENABLE_WEBSOCKETS
    qsanLinkWebSocketGateway();
#endif

    bool hasTestScenarioArgument = qApp->arguments().contains("--test-scenario");
    foreach (const QString &arg, qApp->arguments()) {
        if (arg.startsWith("--test-scenario=")) {
            hasTestScenarioArgument = true;
            break;
        }
    }

    if (qApp->arguments().contains("-server")) {
        Server *server = new Server(qApp);
        printf("Server is starting on port %u\n", Config.ServerPort);

        if (server->listen())
            printf("Starting successfully\n");
        else {
            delete server;
            printf("Starting failed!\n");
        }

        const int rc = qApp->exec();
        CrashHandler::beginShutdown();
        return rc;
    } else if (qApp->arguments().contains("--headless") && !hasTestScenarioArgument) {
        const QStringList args = qApp->arguments();
        const int modeIdx = args.indexOf("--game-mode");
        if (modeIdx >= 0 && modeIdx + 1 < args.size()) {
            const QString modeId = args.at(modeIdx + 1);
            Config.GameMode = Sanguosha->getGameMode(modeId);
            if (!Config.GameMode.isValid()) {
                Server::writeHeadlessLog(QString("ERROR: Unknown game mode '%1'").arg(modeId));
                return 1;
            }
        }
        const int gamesIdx = args.indexOf("--games");
        if (gamesIdx >= 0 && gamesIdx + 1 < args.size()) {
            bool ok = false;
            const int limit = args.at(gamesIdx + 1).toInt(&ok);
            if (ok && limit > 0)
                Server::headlessGameLimit = limit;
        }
        const int logIdx = args.indexOf("--headless-log");
        if (logIdx >= 0 && logIdx + 1 < args.size())
            Server::setHeadlessLogFile(args.at(logIdx + 1));
        foreach (const QString &arg, args) {
            QString name, value;
            if (arg.startsWith("--test-general2=")) {
                name = "--test-general2";
                value = arg.mid(16);
            } else if (arg.startsWith("--test-general=")) {
                name = "--test-general";
                value = arg.mid(15);
            } else if (arg == "--test-general" || arg == "--test-general2") {
                name = arg;
                const int idx = args.indexOf(arg);
                if (idx >= 0 && idx + 1 < args.size())
                    value = args.at(idx + 1);
            }
            if (name.isEmpty() || value.isEmpty() || value.startsWith("-"))
                continue;
            if (name == "--test-general2")
                Server::forcedHeadlessGeneral2 = value;
            else
                Server::forcedHeadlessGeneral = value;
        }
        Server::writeHeadlessLog(QString("[AUTOTEST] forced general: main='%1' deputy='%2'")
            .arg(Server::forcedHeadlessGeneral, Server::forcedHeadlessGeneral2));
        Server *server = new Server(qApp);
        qDebug() << ">>> Headless Mode: Starting stress test with"
                 << Server::headlessGameLimit << "games, mode" << Config.GameMode.mode_id << "<<<";
        QTimer::singleShot(0, server, &Server::startHeadlessGame);
        const int rc = qApp->exec();
        CrashHandler::beginShutdown();
        return rc;
    }

    auto getTestScenarioArg = []() -> QString {
        foreach (QString arg, qApp->arguments()) {
            if (arg.startsWith("--test-scenario=")) {
                return arg.mid(16);
            }
        }
        int idx = qApp->arguments().indexOf("--test-scenario");
        if (idx >= 0 && idx + 1 < qApp->arguments().size()) {
            return qApp->arguments().at(idx + 1);
        }
        return QString();
    };

    QString testScenario = getTestScenarioArg();
    if (!testScenario.isEmpty()) {
        bool headless = qApp->arguments().contains("--headless") || qApp->arguments().contains("-h");

        if (!Sanguosha->loadTestScenario(testScenario)) {
            qDebug() << "Failed to load test scenario:" << testScenario;
            return 1;
        }

        Config.GameMode = Sanguosha->getGameMode("test_scenario");
        Config.setValue("GameMode", "test_scenario");

        Server *server = new Server(qApp);

        if (!headless) {
            QFile file("qss/sanguosha.qss");
            if (file.open(QIODevice::ReadOnly)) {
                QTextStream stream(&file);
                qApp->setStyleSheet(stream.readAll());
            }

            MainWindow *main_window = new MainWindow;
            Sanguosha->setParent(main_window);
            main_window->show();

#ifdef AUDIO_SUPPORT
            Audio::init();
            Config.FrontBGMVolume = Config.value("FrontBGMVolume", 1.0f).toFloat();
            if (Config.FrontBGMVolume > 0 && QFile::exists("audio/system/BGM/front-bgm.ogg")) {
                Audio::playBGM("audio/system/BGM/front-bgm.ogg");
                Audio::setBGMVolume(Config.FrontBGMVolume);
            }
#endif

            Config.HostAddress = "127.0.0.1";
            Config.setValue("HostAddress", "127.0.0.1");
            Config.UserName = "Player";
            Config.setValue("UserName", "Player");
            Config.setValue("EnableReconnection", true);

            QTimer::singleShot(1000, main_window, &MainWindow::startConnection);
        }

        qDebug() << ">>> Test Scenario Mode:" << testScenario << (headless ? "(headless)" : "(with GUI)") << "<<<";
        QTimer::singleShot(0, [server, testScenario, headless]() {
            server->startTestGame(testScenario, headless);
        });
        const int rc = qApp->exec();
        CrashHandler::beginShutdown();
        return rc;
    }

    startupPhase.next("main.stylesheet");
    QFile file("qss/sanguosha.qss");
    if (file.open(QIODevice::ReadOnly)) {
        QTextStream stream(&file);
        qApp->setStyleSheet(stream.readAll());
    }

    startupPhase.finish();
    startupTotal.finish();
    QSanStartupTiming::flushAggregates();
    bool hasLocalResponseUiCase = false;
    for (const QString &argument : arguments) {
        if (argument == QStringLiteral("--local-response-ui-case")
            || argument.startsWith(QStringLiteral("--local-response-ui-case="))) {
            hasLocalResponseUiCase = true;
            break;
        }
    }
    if (hasLocalResponseUiCase) {
        const int rc = LocalResponseUiController::run(arguments);
        CrashHandler::beginShutdown();
        return rc;
    }

    if (uiStartupSmoke) {
        const int rc = UiStartupSmokeController::run();
        exitStartupSmoke(rc);
    }

    if (multimediaSmoke) {
        const int rc = MultimediaSmokeController::run();
        CrashHandler::beginShutdown();
        return rc;
    }

    if (effectsSmoke) {
        const int rc = EffectsSmokeController::run();
        CrashHandler::beginShutdown();
        return rc;
    }

    MainWindow *main_window = new MainWindow;
#ifdef Q_OS_ANDROID
    const auto completeAndroidBoot = [&androidContent, main_window] {
        QString error;
        if (!androidContent.markBootSuccessful(&error))
            QMessageBox::warning(main_window, AndroidContentDialog::tr("Cannot finish the startup record"), error);
    };
    if (main_window->isHomeSceneReady()) completeAndroidBoot();
    else QObject::connect(main_window, &MainWindow::homeSceneReady, main_window, completeAndroidBoot,
                          Qt::SingleShotConnection);
#endif
    Sanguosha->setParent(main_window);
    main_window->show();

    const auto releaseGui = [main_window] {
        // Preserve the Engine lifetime boundary; GUI deletion must not trigger native or Lua teardown through MainWindow children.
        Sanguosha->setParent(nullptr);
        delete main_window;
        // QApplication must release the shared OpenGL context before Qt's static caches are destroyed.
        delete QCoreApplication::instance();
    };

#ifdef AUDIO_SUPPORT
    Audio::init();
	Config.FrontBGMVolume = Config.value("FrontBGMVolume", 1.0f).toFloat();
	if (Config.FrontBGMVolume>0&&QFile::exists("audio/system/BGM/front-bgm.ogg")){
		Audio::playBGM("audio/system/BGM/front-bgm.ogg");
		Audio::setBGMVolume(Config.FrontBGMVolume);
	}
#endif

    foreach (QString arg, qApp->arguments()) {
        if (arg == "--auto-robots") {
            Config.AutoAddRobots = true;
            continue;
        }
        if (arg == "--test-general2" || arg.startsWith("--test-general2=")) {
            QString general = arg.mid(arg.indexOf('=') + 1);
            if (general == arg) {
                const int idx = qApp->arguments().indexOf(arg);
                if (idx >= 0 && idx + 1 < qApp->arguments().size())
                    general = qApp->arguments().at(idx + 1);
            }
            if (!general.isEmpty() && !general.startsWith("-"))
                Config.AutoPickGeneral2 = general;
            continue;
        }
        if (arg.startsWith("--test-general")) {
            QString general = arg.mid(arg.indexOf('=') + 1);
            if (general == arg) {
                const int idx = qApp->arguments().indexOf(arg);
                if (idx >= 0 && idx + 1 < qApp->arguments().size())
                    general = qApp->arguments().at(idx + 1);
            }
            if (!general.isEmpty() && !general.startsWith("-"))
                Config.AutoPickGeneral = general;
            continue;
        }
    }

    foreach (QString arg, qApp->arguments()) {
        if (arg.startsWith("-connect:")) {
            arg.remove("-connect:");
            Config.HostAddress = arg;
            Config.setValue("HostAddress", arg);

            main_window->startConnection();
            break;
        }
    }

    // Linux GUI M2 network smoke. Runs after -connect:: by this point Client has been
    // created and the socket has called connectToHost(), but QTcpSocket is asynchronous and
    // connected() only fires once the event loop runs, so the first stage is not missed.
    //
    // This entry point only observes and stands in for a human operator; it never changes
    // the product's connect/room/game flow and never activates on a normal player launch
    // without the explicit flag.
    if (NetworkUiSmokeController::isRequested(arguments)) {
        int smokeExitCode = 0;
        if (!NetworkUiSmokeController::begin(arguments, main_window, &smokeExitCode)) {
            releaseGui();
            return smokeExitCode;
        }
    }

    if (Config.AutoAddRobots || !Config.AutoPickGeneral.isEmpty()) {
        QFile diag("client_autotest_diag.log");
        if (diag.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream(&diag) << QDateTime::currentDateTime().toString("HH:mm:ss.zzz")
                << " main: args=" << qApp->arguments().join(" ")
                << " AutoPickGeneral='" << Config.AutoPickGeneral
                << "' AutoPickGeneral2='" << Config.AutoPickGeneral2 << "'\n";
        }
    }

    const int rc = qApp->exec();
    CrashHandler::beginShutdown();
    // Finalize smoke observations before releasing the scene; timeout and normal close share the same teardown.
    int exitCode = rc;
    if (NetworkUiSmokeController::isRequested(arguments))
        exitCode = NetworkUiSmokeController::finish(rc);
    releaseGui();
    return exitCode;
}

#include <cstring>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QApplication>
#include <QCoreApplication>
#include <QStringList>

#include "mainwindow.h"
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

#ifdef ANDROID
#include "android_assets.h"
#endif

#include "crashhandler.h"
#include "testing/local-response-ui-controller.h"
#include "testing/ui-startup-smoke-controller.h"

int main(int argc, char *argv[]) {
    CrashHandler::install();
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--local-response-ui-capabilities") == 0) {
            fputs("{\"schema_version\":1,\"auto\":true,\"show\":true,\"inspect\":true}\n", stdout);
            fflush(stdout);
            return 0;
        }
    }
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--seed") == 0) {
            qputenv("QT_HASH_SEED", "0");
            break;
        }
    }

    // 隱藏入口:-crashtest <type> 觸發一次崩潰用於驗證 crash handler
    if (argc > 2 && strcmp(argv[1], "-crashtest") == 0) {
        new QCoreApplication(argc, argv); // selfTest 需要 CWD
        CrashHandler::selfTest(argv[2]);
        return 0; // 正常情況不會執行到這裡
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

    // QOpenGLWidget and QQuickWidget must use the same graphics API when
    // they are composed in the same top-level window.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

#ifdef ANDROID
	AndroidAssets::copyAssetsToWritableLocation();
#endif
    // headless 模式 (-server / --headless) 只用 QCoreApplication,
    // 不載入 QPA 平台插件與 QtWidgets/QML:記憶體↓、無桌面環境可跑、啟動加快。
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

    // --ui-startup-smoke 係真正的 GUI startup 驗證，一定要行 QApplication path，
    // 唔可以被 headless 判斷截走。
    const bool uiStartupSmoke = UiStartupSmokeController::isRequested(appArgs);

    const bool headlessApp = !uiStartupSmoke
        && (appArgs.contains("-server")
            || appArgs.contains("--headless")
            || (hasTestScenarioArg && appArgs.contains("-h")));

    if (argc > 1 && strcmp(argv[1], "-manual") == 0) {
        new QCoreApplication(argc, argv);
        if (!EngineBootstrap::initialize(true))
            return 1;
        return 0;
    } else if (headlessApp)
        new QCoreApplication(argc, argv);
    else {
        new QApplication(argc, argv);
        // 主頁自訂 contentItem／indicator；Windows 原生樣式不支援會報錯並閃爍
        QQuickStyle::setStyle(QStringLiteral("Basic"));
    }

    // startup smoke：QApplication 已經建立，喺度接手 Qt message hook 並登記
    // application stage。呢個入口任何情況都唔會喺 QApplication 之前 return。
    if (uiStartupSmoke) {
        int smokeExitCode = 0;
        if (!UiStartupSmokeController::begin(qApp->arguments(), &smokeExitCode))
            return smokeExitCode;
    }

    // 美術 PNG 內嵌的 iCCP chunk 帶有錯誤的 sRGB profile，libpng 1.6+ 會對每張
    // 圖發出 "known incorrect sRGB profile" warning（qt.gui.imageio category）。
    // 純屬無害的色彩描述檔警告，在此統一靜音，避免 console 被刷滿。
    QLoggingCategory::setFilterRules(QStringLiteral("qt.gui.imageio.warning=false"));

#ifdef Q_OS_WIN
    qputenv("QT_MEDIA_BACKEND", "ffmpeg");
#endif

    // 自動化測試: 提早解析 --headless-log, 讓初始化階段的錯誤也能寫入標記檔
    // (GUI 子系統 stdout 不可見, headless 錯誤必須走標記檔)
    const int earlyLogIdx = qApp->arguments().indexOf("--headless-log");
    if (earlyLogIdx >= 0 && earlyLogIdx + 1 < qApp->arguments().size())
        Server::setHeadlessLogFile(qApp->arguments().at(earlyLogIdx + 1));

    qsrand(QTime(0, 0, 0).secsTo(QTime::currentTime()));

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
    // 若 exe 旁放了 Qt6 DLL，Qt 會把 prefix 重定位到 exe 目錄，導致 multimedia 後端
    // （plugins/multimedia/ffmpegmediaplugin.dll）在 exe 旁的 plugins 找不到。
    // 把 Qt 安裝的 plugins 目錄也加入搜尋路徑，確保影片背景可播放。
    // 這是 Windows DLL 搜尋的補救；Linux 由 distro Qt 自行解析 plugin 路徑，
    // 毋須改 PATH，所以整段只在 Windows 生效。
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

#ifdef Q_OS_MAC
#ifdef QT_NO_DEBUG
    QDir::setCurrent(qApp->applicationDirPath());
#endif
#endif

#ifdef Q_OS_LINUX
    QDir dir("lua");
    if (dir.exists() && (dir.exists("config.lua"))) {
        // things look good and use current dir
    } else
        QDir::setCurrent(qApp->applicationFilePath().replace("games", "share"));
#endif

    QTranslator qt_translator, translator;
    qt_translator.load("qt_zh_CN.qm");
    translator.load("sanguosha.qm");

    qApp->installTranslator(&qt_translator);
    qApp->installTranslator(&translator);

    if (!EngineBootstrap::initialize()) {
        Server::writeHeadlessLog("ERROR: EngineBootstrap::initialize failed");
        if (uiStartupSmoke)
            return UiStartupSmokeController::abortEarly(QStringLiteral("engine"),
                QStringLiteral("EngineBootstrap::initialize failed"), 1);
        return 1;
    }
    // Engine 已就緒,把真實版本號補登記給 crash handler(install() 時拿不到)
    CrashHandler::setVersion(Sanguosha->getVersionNumber().toUtf8().constData());
#ifdef AUDIO_SUPPORT
    QObject::connect(Sanguosha, &Engine::audioEffectRequested,
                     [](const QString &filename, bool superpose) { Audio::play(filename, superpose); });
#endif
    Config.init();
    // UiConfig 載入 QFontDatabase/QFont,必須在有 QGuiApplication 的環境才安全;
    // headless(QCoreApplication)直接跳過,字型與 palette 只有 GUI 需要。
    if (qobject_cast<QApplication *>(qApp))
        UiConfig.init();
    applyColorScheme(Config.ColorScheme);
    applyVisualMode(Config.VisualMode);
    if (qobject_cast<QApplication *>(qApp))
        qApp->setFont(UiConfig.AppFont);
    BanPair::loadBanPairs();

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
        CrashHandler::beginShutdown(); // 正常關閉流程,退出清理階段的崩潰不再上報
        return rc;
    } else if (qApp->arguments().contains("--headless") && !hasTestScenarioArgument) {
        // 自動化測試: 以指定模式與局數執行 headless 壓力測試
        // 用法: QSanguosha.exe --headless [--game-mode 10p] [--games 100]
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
        // 自動化測試: headless 指定主公武將 (本分支提前 return, 需自行解析)
        //   --test-general <主將>  /  --test-general2 <副將> (雙將模式)
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
        // 自動化測試 diag: 記錄指定武將, 供 runner 驗證指定生效
        Server::writeHeadlessLog(QString("[AUTOTEST] forced general: main='%1' deputy='%2'")
            .arg(Server::forcedHeadlessGeneral, Server::forcedHeadlessGeneral2));
        Server *server = new Server(qApp);
        qDebug() << ">>> Headless Mode: Starting stress test with"
                 << Server::headlessGameLimit << "games, mode" << Config.GameMode.mode_id << "<<<";
        QTimer::singleShot(0, server, &Server::startHeadlessGame);
        const int rc = qApp->exec();
        CrashHandler::beginShutdown(); // 正常關閉流程,退出清理階段的崩潰不再上報
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
        CrashHandler::beginShutdown(); // 正常關閉流程,退出清理階段的崩潰不再上報
        return rc;
    }

    QFile file("qss/sanguosha.qss");
    if (file.open(QIODevice::ReadOnly)) {
        QTextStream stream(&file);
        qApp->setStyleSheet(stream.readAll());
    }

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
        // 由呢度開始同正常啟動走同一條路：建立真正的 MainWindow、載入真正的
        // HomeScene、行真正的 Qt event loop，等 ready condition 之後自動退出。
        const int rc = UiStartupSmokeController::run();
        CrashHandler::beginShutdown();
        return rc;
    }

    MainWindow *main_window = new MainWindow;
    Sanguosha->setParent(main_window);
    main_window->show();

#ifdef AUDIO_SUPPORT
    Audio::init();
	Config.FrontBGMVolume = Config.value("FrontBGMVolume", 1.0f).toFloat();
	if (Config.FrontBGMVolume>0&&QFile::exists("audio/system/BGM/front-bgm.ogg")){
		Audio::playBGM("audio/system/BGM/front-bgm.ogg");
		Audio::setBGMVolume(Config.FrontBGMVolume);
	}
#endif

    // 自動化測試參數先解析 (獨立迴圈, 不依賴參數順序)
    foreach (QString arg, qApp->arguments()) {
        if (arg == "--auto-robots") {
            // 自動化測試: owner 進入房間後自動填滿 AI 並開局
            Config.AutoAddRobots = true;
            continue;
        }
        if (arg == "--test-general2" || arg.startsWith("--test-general2=")) {
            // 自動化測試: 雙將模式副將 (--test-general2=<name> 或 --test-general2 <name>)
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
            // 自動化測試: 自動選將 (--test-general=<name> 或 --test-general <name>)
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

    // 自動化測試診斷
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
    CrashHandler::beginShutdown(); // 正常關閉流程,退出清理階段的崩潰不再上報
    return rc;
}

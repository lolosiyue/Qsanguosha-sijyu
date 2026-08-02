#include <cstring>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QApplication>
#include <QCoreApplication>
#include <QStringList>

#include "mainwindow.h"
#include "settings.h"
#include "banpair.h"
#include "server.h"
#include "ai.h"
#include "serverplayer.h"
#include "room.h"
#include "engine.h"
#include "engine-bootstrap.h"
#include "lua.hpp"
#include "lua-wrapper.h"
#include "audio.h"
#include <QSurfaceFormat>
#include <QQuickWindow>
#include <QSGRendererInterface>

#ifdef ANDROID
#include "android_assets.h"
#endif

#if defined(WIN32) && defined(USE_BREAKPAD)
#include <direct.h>
#include "breakpad/client/windows/handler/exception_handler.h"

using namespace google_breakpad;

static bool callback(const wchar_t *dump_path, const wchar_t *id, void *, EXCEPTION_POINTERS *, MDRawAssertionInfo *, bool succeeded){
    if (succeeded)
        qWarning("Dump file created in %s, dump guid is %ws\n", dump_path, id);
    else
        qWarning("Dump failed\n");
    return succeeded;
}

// dump 目錄固定在 exe 旁的 dmp\，避免受啟動時工作目錄影響
static std::wstring dumpDirectory(){
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring dir(buf, len);
    size_t pos = dir.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
        dir.resize(pos);
    return dir + L"\\dmp";
}

int main(int argc, char *argv[]) {
    _wmkdir(dumpDirectory().c_str());
    ExceptionHandler eh(dumpDirectory(), nullptr, callback, nullptr, ExceptionHandler::HANDLER_ALL);
#else
int main(int argc, char *argv[])
{
#endif

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
    // headless 模式 (-server / --headless / --lua-test) 只用 QCoreApplication,
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

    const bool headlessApp = appArgs.contains("-server")
        || appArgs.contains("--lua-test")
        || appArgs.contains("--headless")
        || (hasTestScenarioArg && appArgs.contains("-h"));

    if (argc > 1 && strcmp(argv[1], "-manual") == 0) {
        new QCoreApplication(argc, argv);
        if (!EngineBootstrap::initialize(true))
            return 1;
        return 0;
    } else if (headlessApp)
        new QCoreApplication(argc, argv);
    else
        new QApplication(argc, argv);

#ifdef Q_OS_WIN
    qputenv("QT_MEDIA_BACKEND", "ffmpeg");
#endif

    // 自動化測試: 提早解析 --headless-log, 讓初始化階段的錯誤也能寫入標記檔
    // (GUI 子系統 stdout 不可見, headless 錯誤必須走標記檔)
    const int earlyLogIdx = qApp->arguments().indexOf("--headless-log");
    if (earlyLogIdx >= 0 && earlyLogIdx + 1 < qApp->arguments().size())
        Server::setHeadlessLogFile(qApp->arguments().at(earlyLogIdx + 1));

    QCoreApplication::addLibraryPath(QCoreApplication::applicationDirPath() + "/plugins");

    // 若 exe 旁放了 Qt6 DLL，Qt 會把 prefix 重定位到 exe 目錄，導致 multimedia 後端
    // （plugins/multimedia/ffmpegmediaplugin.dll）在 exe 旁的 plugins 找不到。
    // 把 Qt 安裝的 plugins 目錄也加入搜尋路徑，確保影片背景可播放。
    const QString qtBinDir = QStringLiteral(QT_BIN_DIR);
    QCoreApplication::addLibraryPath(QDir(qtBinDir).filePath("../plugins"));
    QString path = qEnvironmentVariable("PATH");
    if (!path.contains(qtBinDir, Qt::CaseInsensitive)) {
        if (!path.isEmpty())
            path.prepend(QLatin1Char(';'));
        path.prepend(qtBinDir);
        qputenv("PATH", path.toUtf8());
    }

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

    // initialize random seed for later use
    qsrand(QTime(0, 0, 0).secsTo(QTime::currentTime()));

    QTranslator qt_translator, translator;
    qt_translator.load("qt_zh_CN.qm");
    translator.load("sanguosha.qm");

    qApp->installTranslator(&qt_translator);
    qApp->installTranslator(&translator);

    if (!EngineBootstrap::initialize()) {
        Server::writeHeadlessLog("ERROR: EngineBootstrap::initialize failed");
        return 1;
    }
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

    if (qApp->arguments().contains("--lua-test")) {
        int idx = qApp->arguments().indexOf("--lua-test");
        QString scriptPath;
        if (idx + 1 < qApp->arguments().size())
            scriptPath = qApp->arguments().at(idx + 1);

        if (scriptPath.isEmpty()) {
            printf("Usage: QSanguosha.exe --lua-test <script.lua>\n");
            return 1;
        }

        bool verbose = qApp->arguments().contains("--lua-test-verbose");
        Server::isHeadlessMode = true;
        printf(">>> Lua Test Mode: %s <<<\n", qPrintable(scriptPath));

        lua_State *L = Sanguosha->getLuaState();
        if (!L) {
            printf("ERROR: Failed to get Lua state\n");
            return 1;
        }

        if (!DoLuaScript(L, "lua/test/runner.lua")) {
            printf("ERROR: Failed to load lua/test/runner.lua\n");
            return 1;
        }
        if (!DoLuaScript(L, scriptPath.toLocal8Bit().constData())) {
            printf("ERROR: Failed to load test script: %s\n", qPrintable(scriptPath));
            return 1;
        }

        if (lua_gettop(L) < 1 || (!lua_istable(L, -1) && !lua_isfunction(L, -1) && !lua_isuserdata(L, -1))) {
            printf("ERROR: Test script must return a runner table\n");
            return 1;
        }

        QString tmpPath = QDir::tempPath() + "/sgs_lua_test_scene.txt";
        QFile tmpFile(tmpPath);
        if (!tmpFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            printf("ERROR: Cannot write temp scenario file\n");
            return 1;
        }
        tmpFile.write("general:sujiang|role:lord|hp:4|starter\n");
        tmpFile.write("general:sujiang|role:rebel|hp:4\n");
        tmpFile.write("extraOptions:singleTurn:lord\n");
        tmpFile.close();
        Sanguosha->loadTestScenario(tmpPath);

        Config.GameMode = Sanguosha->getGameMode("test_scenario");
        Config.setValue("GameMode", "test_scenario");

        Server *server = new Server(qApp);
        Room *room = server->createNewRoom();
        if (!room) {
            printf("ERROR: Failed to create room\n");
            return 1;
        }

        int playerCount = Sanguosha->getTestScenarioPlayerCount();
        for (int i = 0; i < playerCount; i++) {
            ServerPlayer *player = room->addAIPlayer();
            player->setAI(new TrustAI(player));
            if (i == 0) player->setOwner(true);
            room->signup(player, QString("Test_%1").arg(i), "", true);
        }

        room->initializeLuaTestEnvironment();

        bool luaTestPassed = true;
        QPointer<Room> roomPtr(room);
        QObject::connect(room, &Room::game_over, qApp, [L, verbose, roomPtr, &luaTestPassed](const QString &winner) {
            if (verbose)
                Server::writeHeadlessLog(QString("Game over. Winner: %1").arg(winner));

            lua_getglobal(L, "RUNNER_DO_ASSERTIONS");
            if (lua_isfunction(L, -1)) {
                if (lua_pcall(L, 0, 1, 0) != 0) {
                    const QString err = luaErrorWithTraceback(L);
                    printf("ERROR in assertions: %s\n", qUtf8Printable(err));
                    lua_pop(L, 1);
                    luaTestPassed = false;
                } else {
                    luaTestPassed = lua_toboolean(L, -1);
                    lua_pop(L, 1);
                }
            } else {
                lua_pop(L, 1);
                printf("ERROR: Test runner has no assertions callback\n");
                luaTestPassed = false;
            }

            if (roomPtr) {
                roomPtr->clearTestOverrides();
            }

            const int exitCode = luaTestPassed ? 0 : 1;
            QTimer::singleShot(0, qApp, [exitCode]() { qApp->exit(exitCode); });
        });

        int topBefore = lua_gettop(L);
        lua_getfield(L, -1, "execute");
        if (lua_isfunction(L, -1)) {
            if (lua_pcall(L, 0, 0, 0) != 0) {
                const QString err = luaErrorWithTraceback(L);
                printf("ERROR: %s\n", qUtf8Printable(err));
                lua_pop(L, 1);
                Server::writeHeadlessLog(QString("Test execution failed: %1").arg(err));
                return 1;
            }
        } else {
            lua_pop(L, 1);
            printf("ERROR: Runner has no execute() method\n");
            return 1;
        }
        lua_settop(L, topBefore);

        return qApp->exec();
    }

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

        return qApp->exec();
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
        return qApp->exec();
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
        return qApp->exec();
    }

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

    return qApp->exec();
}

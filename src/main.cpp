#include <cstring>
#include <QTimer>
#include <QDir>
#include <QFile>

#include "mainwindow.h"
#include "settings.h"
#include "banpair.h"
#include "server.h"
#include "ai.h"
//#include "serverplayer.h"
#include "engine.h"
#include <QSurfaceFormat>

extern "C" {
struct swig_type_info;
extern swig_type_info SWIGTYPE_p_Room;
extern void SWIG_NewPointerObj(struct lua_State *L, void *ptr, swig_type_info *type, int own);
}

#ifdef ANDROID
#include "android_assets.h"
#endif

#if defined(WIN32) && defined(VS2013)
#include "breakpad/client/windows/handler/exception_handler.h"

using namespace google_breakpad;

static bool callback(const wchar_t *dump_path, const wchar_t *id, void *, EXCEPTION_POINTERS *, MDRawAssertionInfo *, bool succeeded){
    if (succeeded)
        qWarning("Dump file created in %s, dump guid is %ws\n", dump_path, id);
    else
        qWarning("Dump failed\n");
    return succeeded;
}

int main(int argc, char *argv[]) {
    ExceptionHandler eh(L"./dmp", nullptr, callback, nullptr, ExceptionHandler::HANDLER_ALL);
#else
int main(int argc, char *argv[])
{
#endif

    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setAlphaBufferSize(8);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    QSurfaceFormat::setDefaultFormat(format);

#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

#ifdef ANDROID
	AndroidAssets::copyAssetsToWritableLocation();
#endif
    if (argc > 1 && strcmp(argv[1], "-server") == 0)
        new QCoreApplication(argc, argv);
    else if (argc > 1 && strcmp(argv[1], "-manual") == 0) {
        new QCoreApplication(argc, argv);
        Sanguosha = new Engine(true);
        return 0;
    } else if (argc > 1 && strcmp(argv[1], "--lua-test") == 0)
        new QCoreApplication(argc, argv);
    else
       new QApplication(argc, argv);

    QCoreApplication::addLibraryPath(QCoreApplication::applicationDirPath() + "/plugins");

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

    Sanguosha = new Engine;
    Config.init();
    qApp->setFont(Config.AppFont);
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
        if (!DoLuaScript(L, scriptPath)) {
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

        Config.GameMode = GameModeStruct("test_scenario");
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

        SWIG_NewPointerObj(L, room, SWIGTYPE_p_Room, 0);
        lua_setglobal(L, "ROOM");

        QPointer<Room> roomPtr(room);
        QObject::connect(room, &Room::game_over, qApp, [L, verbose, roomPtr](const QString &winner) {
            if (verbose)
                Server::writeHeadlessLog(QString("Game over. Winner: %1").arg(winner));

            lua_getglobal(L, "RUNNER_DO_ASSERTIONS");
            if (lua_isfunction(L, -1)) {
                if (lua_pcall(L, 0, 0, 0) != 0) {
                    const char *err = lua_tostring(L, -1);
                    printf("ERROR in assertions: %s\n", err ? err : "unknown");
                    lua_pop(L, 1);
                }
            } else {
                lua_pop(L, 1);
            }

            if (roomPtr) {
                roomPtr->clearTestOverrides();
            }

            QTimer::singleShot(0, qApp, &QCoreApplication::quit);
        });

        int topBefore = lua_gettop(L);
        lua_getfield(L, -1, "execute");
        if (lua_isfunction(L, -1)) {
            if (lua_pcall(L, 0, 0, 0) != 0) {
                const char *err = lua_tostring(L, -1);
                printf("ERROR: %s\n", err ? err : "unknown");
                lua_pop(L, 1);
                Server::writeHeadlessLog(QString("Test execution failed: %1").arg(err ? err : "unknown"));
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
    } else if (qApp->arguments().contains("--headless")) {
        Server *server = new Server(qApp);
        qDebug() << ">>> Headless Mode: Starting stress test with 10000 games <<<";
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

        Config.GameMode = GameModeStruct("test_scenario");
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

    foreach (QString arg, qApp->arguments()) {
        if (arg.startsWith("-connect:")) {
            arg.remove("-connect:");
            Config.HostAddress = arg;
            Config.setValue("HostAddress", arg);

            main_window->startConnection();
            break;
        }
    }

    return qApp->exec();
}
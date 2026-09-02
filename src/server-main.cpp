#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>
#include <QVariantMap>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(Q_OS_UNIX)
#include <csignal>
#endif

#include "core/asset-manifest.h"
#include "core/engine-bootstrap.h"
#include "core/engine.h"
#include "core/runtime-paths.h"
#include "core/settings.h"
#include "core/version.h"
#include "server/server-command-line.h"
#include "server/server-config.h"
#include "server/server-console.h"
#include "server/server-core.h"
#include "server/server-logger.h"
#include "crashhandler.h"
#include "websocket-gateway.h"

namespace
{
constexpr int CliUsageExitCode = 64;
constexpr int LogFileExitCode = 73;
constexpr int ConfigErrorExitCode = 78;

#if defined(Q_OS_WIN)
volatile LONG shutdownSignal = -1;

BOOL WINAPI requestShutdown(DWORD controlType)
{
    switch (controlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        InterlockedExchange(&shutdownSignal, static_cast<LONG>(controlType));
        return TRUE;
    default:
        return FALSE;
    }
}

int requestedShutdownCode()
{
    return static_cast<int>(InterlockedCompareExchange(&shutdownSignal, -1, -1));
}
#elif defined(Q_OS_UNIX)
volatile std::sig_atomic_t shutdownSignal = -1;

void requestShutdown(int signal)
{
    shutdownSignal = signal;
}

int requestedShutdownCode()
{
    return static_cast<int>(shutdownSignal);
}
#endif

void printCommandLineError(const QString &error)
{
    QTextStream err(stderr);
    err << QCoreApplication::applicationName() << ": error: " << error << Qt::endl;
    err << "Try '" << QCoreApplication::applicationName() << " --help' for usage."
        << Qt::endl;
}

void printConfigurationErrors(const QStringList &errors)
{
    QTextStream err(stderr);
    for (const QString &error : errors)
        err << QCoreApplication::applicationName() << ": configuration error: "
            << error << Qt::endl;
}

bool applyCommandLineOptions(const ServerCommandLineOptions &options, QString &error)
{
    if (options.gameMode) {
        const GameModeStruct mode = Sanguosha->getGameMode(*options.gameMode);
        if (!mode.isValid()) {
            error = QStringLiteral("unknown game mode '%1'").arg(*options.gameMode);
            return false;
        }
        Config.GameMode = mode;
    }
    if (options.port)
        Config.ServerPort = *options.port;
    if (options.websocketPort)
        Config.WebSocketPort = *options.websocketPort;
    if (options.bindAddress)
        Config.BindAddress = *options.bindAddress;
    if (options.serverName)
        Config.ServerName = *options.serverName;
    if (options.operationTimeout) {
        Config.OperationNoLimit = *options.operationTimeout == 0;
        if (*options.operationTimeout > 0)
            Config.OperationTimeout = *options.operationTimeout;
    }
    if (options.aiEnabled)
        Config.EnableAI = *options.aiEnabled;
    if (options.aiDelay) {
        Config.OriginAIDelay = *options.aiDelay;
        Config.AIDelay = *options.aiDelay;
    }
    if (options.autotestLog)
        Server::setHeadlessLogFile(*options.autotestLog);

    // Settings::init() records the persisted configuration. Refresh the crash
    // summary after applying one-shot CLI overrides.
    stashGameConfigForCrash();
    return true;
}

void printAvailableGameModes(QTextStream &out)
{
    out << "ID\tPLAYERS\tNAME" << Qt::endl;
    const QMap<QString, GameModeStruct> modes = Sanguosha->getAvailableModes();
    for (auto it = modes.cbegin(); it != modes.cend(); ++it) {
        out << it.key() << '\t' << it.value().player_count << '\t'
            << it.value().display_name << Qt::endl;
    }
}

QVariantMap effectiveServerConfiguration()
{
    QVariantMap values = defaultServerConfigValues();
    for (auto it = values.begin(); it != values.end(); ++it)
        it.value() = Config.value(it.key(), it.value());
    for (const QString &key : Config.allKeys()) {
        if (isKnownServerConfigKey(key))
            values.insert(key, Config.value(key));
    }
    const QVariantMap overrides = Config.valueOverrides();
    for (auto it = overrides.cbegin(); it != overrides.cend(); ++it)
        values.insert(it.key(), it.value());

    values.insert(QStringLiteral("ServerName"), Config.ServerName);
    values.insert(QStringLiteral("GameMode"), Config.GameMode.mode_id);
    values.insert(QStringLiteral("ServerPort"), Config.ServerPort);
    values.insert(QStringLiteral("WebSocketPort"), Config.WebSocketPort);
    values.insert(QStringLiteral("DetectorPort"), Config.DetectorPort);
    values.insert(QStringLiteral("BindAddress"), Config.BindAddress);
    values.insert(QStringLiteral("Address"), Config.Address);
    values.insert(QStringLiteral("OperationTimeout"), Config.OperationTimeout);
    values.insert(QStringLiteral("OperationNoLimit"), Config.OperationNoLimit);
    values.insert(QStringLiteral("CountDownSeconds"), Config.CountDownSeconds);
    values.insert(QStringLiteral("NullificationCountDown"), Config.NullificationCountDown);
    values.insert(QStringLiteral("BanPackages"), Config.BanPackages);
    values.insert(QStringLiteral("RandomSeat"), Config.RandomSeat);
    values.insert(QStringLiteral("EnableCheat"), Config.EnableCheat);
    values.insert(QStringLiteral("FreeChoose"), Config.FreeChoose);
    values.insert(QStringLiteral("FreeAssignSelf"), Config.FreeAssignSelf);
    values.insert(QStringLiteral("ForbidSIMC"), Config.ForbidSIMC);
    values.insert(QStringLiteral("DisableChat"), Config.DisableChat);
    values.insert(QStringLiteral("Enable2ndGeneral"), Config.Enable2ndGeneral);
    values.insert(QStringLiteral("EnableSame"), Config.EnableSame);
    values.insert(QStringLiteral("EnableBasara"), Config.EnableBasara);
    values.insert(QStringLiteral("EnableHegemony"), Config.EnableHegemony);
    values.insert(QStringLiteral("EnableMeleeMode"), Config.EnableMeleeMode);
    values.insert(QStringLiteral("MaxHpScheme"), Config.MaxHpScheme);
    values.insert(QStringLiteral("Scheme0Subtraction"), Config.Scheme0Subtraction);
    values.insert(QStringLiteral("PreventAwakenBelow3"), Config.PreventAwakenBelow3);
    values.insert(QStringLiteral("EnableAI"), Config.EnableAI);
    values.insert(QStringLiteral("OriginAIDelay"), Config.OriginAIDelay);
    values.insert(QStringLiteral("AlterAIDelayAD"), Config.AlterAIDelayAD);
    values.insert(QStringLiteral("AIDelayAD"), Config.AIDelayAD);
    values.insert(QStringLiteral("SurrenderAtDeath"), Config.SurrenderAtDeath);
    values.insert(QStringLiteral("EnableLuckCard"), Config.EnableLuckCard);
    values.insert(QStringLiteral("DisableLua"), Config.DisableLua);
    values.insert(QStringLiteral("AddGodGeneral"), Config.AddGodGeneral);
    return values;
}
}

int main(int argc, char **argv)
{
    // QHash may initialize while parsing the command line. Select deterministic
    // hashing before QCoreApplication whenever a game seed was requested.
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--seed") == 0 || qstrncmp(argv[i], "--seed=", 7) == 0
            || qstrcmp(argv[i], "-s") == 0 || qstrncmp(argv[i], "-s=", 3) == 0) {
            qputenv("QT_HASH_SEED", "0");
            break;
        }
    }

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("qsanguosha_server"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(QSanVersion::Number));

    const ServerCommandLineResult commandLine = parseServerCommandLine(app.arguments());
    if (!commandLine.success) {
        printCommandLineError(commandLine.error);
        return CliUsageExitCode;
    }
    const ServerCommandLineOptions &options = commandLine.options;
    QTextStream out(stdout);
    if (options.helpRequested) {
        out << serverCommandLineHelpText();
        return 0;
    }
    if (options.versionRequested) {
        out << QCoreApplication::applicationName() << ' '
            << QCoreApplication::applicationVersion() << Qt::endl;
        return 0;
    }

    if (options.configFile) {
        const ServerConfigLoadResult configFile = loadServerConfigFile(*options.configFile);
        if (!configFile.success) {
            printConfigurationErrors(configFile.errors);
            return ConfigErrorExitCode;
        }
        Config.setValueOverrides(configFile.values);
    }

    // 執行期版面要喺 engine bootstrap 之前解析:engine 直接 load "lua/config.lua",
    // 冇一個真正嘅 asset root 佢會喺 constructor 入面 exit(1)。
    {
        // --asset-root 已經喺 app.arguments() 入面;喺度只係確認佢通過咗
        // CLI 驗證先至用,唔會靜靜接受一個空值。
        QString pathError;
        if (!QSanRuntimePaths::resolve(app.arguments(), &pathError)) {
            QTextStream err(stderr);
            err << pathError << Qt::endl;
            for (const QString &line : QSanRuntimePaths::resolution().candidates)
                err << "  tried " << line << Qt::endl;
            return ConfigErrorExitCode;
        }
    }

    if (options.assetReport) {
        QVariantMap payload;
        payload.insert(QStringLiteral("schema_version"), 1);
        payload.insert(QStringLiteral("runtime_paths"), QSanRuntimePaths::describe());
        const QSanAssetManifest::Report assetReport = QSanAssetManifest::inspect(
            QString(), options.assetManifest.value_or(QString()));
        payload.insert(QStringLiteral("assets"), QSanAssetManifest::describe(assetReport));
        out << QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(payload))
                                     .toJson(QJsonDocument::Indented));
        QTextStream err(stderr);
        for (const QString &line : QSanAssetManifest::diagnostics(assetReport))
            err << line << Qt::endl;
        return assetReport.complete() ? 0 : 7;
    }

    const bool startsServer = !options.listGameModes
        && !options.printConfig && !options.checkConfig;
    ServerLogger logger;
    QString error;
    if (startsServer) {
        ServerLogConfiguration logConfiguration;
        if (options.logLevel
            && !parseServerLogLevel(*options.logLevel, logConfiguration.level)) {
            printCommandLineError(QStringLiteral("invalid log level '%1'")
                .arg(*options.logLevel));
            return CliUsageExitCode;
        }
        if (options.logFormat
            && !parseServerLogFormat(*options.logFormat, logConfiguration.format)) {
            printCommandLineError(QStringLiteral("invalid log format '%1'")
                .arg(*options.logFormat));
            return CliUsageExitCode;
        }
        if (options.logFile)
            logConfiguration.filePath = *options.logFile;
        if (!logger.start(logConfiguration, error)) {
            printCommandLineError(error);
            return LogFileExitCode;
        }
    }

    CrashHandler::install();
    if (options.seed) {
        QString seedError;
        if (!Server::configureGameSeed(QString::number(*options.seed), &seedError)) {
            printCommandLineError(seedError);
            return CliUsageExitCode;
        }
    }

    if (!EngineBootstrap::initialize(false, &error)) {
        if (startsServer)
            logger.error(QStringLiteral("engine"), error);
        else
            QTextStream(stderr) << error << Qt::endl;
        return 1;
    }
    // The dedicated entry point owns explicit teardown after Server/Room cleanup.
    // Avoid the legacy GUI lifetime hook deleting the engine during aboutToQuit().
    QObject::disconnect(&app, SIGNAL(aboutToQuit()), Sanguosha, SLOT(deleteLater()));

    CrashHandler::setVersion(Sanguosha->getVersionNumber().toUtf8().constData());
    if (options.configFile) {
        const QVariantMap overrides = Config.valueOverrides();
        const auto mode = overrides.constFind(QStringLiteral("GameMode"));
        if (mode != overrides.cend() && !Sanguosha->getGameMode(mode->toString()).isValid()) {
            printConfigurationErrors({QStringLiteral("GameMode: unknown game mode '%1'")
                                          .arg(mode->toString())});
            EngineBootstrap::shutdown();
            return ConfigErrorExitCode;
        }
    }
    if (options.listGameModes && !options.printConfig && !options.checkConfig) {
        printAvailableGameModes(out);
        EngineBootstrap::shutdown();
        return 0;
    }

    Config.init();
    if (!applyCommandLineOptions(options, error)) {
        printCommandLineError(error);
        EngineBootstrap::shutdown();
        return CliUsageExitCode;
    }
    const QVariantMap effectiveConfig = effectiveServerConfiguration();
    if (options.checkConfig) {
        const QStringList configErrors = validateServerConfigValues(effectiveConfig);
        if (!configErrors.isEmpty()) {
            printConfigurationErrors(configErrors);
            EngineBootstrap::shutdown();
            return ConfigErrorExitCode;
        }
    }
    if (options.listGameModes)
        printAvailableGameModes(out);
    if (options.printConfig)
        out << (options.jsonOutput ? serverConfigJson(effectiveConfig)
                                  : serverConfigText(effectiveConfig));
    if (options.checkConfig && !options.printConfig)
        out << "Configuration OK" << Qt::endl;
    if (options.listGameModes || options.printConfig || options.checkConfig) {
        EngineBootstrap::shutdown();
        return 0;
    }

#if defined(Q_OS_WIN)
    if (!SetConsoleCtrlHandler(requestShutdown, TRUE)) {
        logger.error(QStringLiteral("server"),
            QStringLiteral("Unable to install the Windows console control handler"));
        EngineBootstrap::shutdown();
        return 1;
    }
#elif defined(Q_OS_UNIX)
    std::signal(SIGINT, requestShutdown);
    std::signal(SIGTERM, requestShutdown);
#endif
    Server::isHeadlessMode = true;
    int result;
    {
#if QSAN_ENABLE_WEBSOCKETS
        qsanLinkWebSocketGateway();
#endif
        Server server(&app);
        ServerConsole console(&server, &app);
#if defined(Q_OS_UNIX) || defined(Q_OS_WIN)
        QTimer shutdownTimer;
        shutdownTimer.setInterval(100);
        QObject::connect(&shutdownTimer, &QTimer::timeout, &app,
            [&app, &console, &logger]() {
                const int shutdownCode = requestedShutdownCode();
                if (shutdownCode < 0)
                    return;
#if defined(Q_OS_WIN)
                const QString message = QStringLiteral(
                    "Shutdown requested by console control %1").arg(shutdownCode);
                const QVariantMap details {
                    {QStringLiteral("control_event"), shutdownCode},
                };
#else
                const QString message = QStringLiteral("Shutdown requested by signal %1")
                    .arg(shutdownCode);
                const QVariantMap details {
                    {QStringLiteral("signal"), shutdownCode},
                };
#endif
                logger.info(QStringLiteral("server"), message, -1, QString(), details);
                if (console.isInteractive())
                    console.writeLog(message);
                app.quit();
            });
        shutdownTimer.start();
#endif
        QObject::connect(&server, &Server::logMessage, &app,
            [&console, &logger](const QString &message) {
                logger.info(QStringLiteral("server"), message);
                if (console.isInteractive())
                    console.writeLog(message);
            });
        QObject::connect(&server, &Server::roomLogMessage, &app,
            [&console, &logger](int roomId, const QString &message) {
                logger.info(QStringLiteral("room"), message, roomId);
                if (console.isInteractive())
                    console.writeLog(message);
            });
        QObject::connect(&server, &Server::playerJoined, &app,
            [&logger](const QString &playerId, const QString &playerName, int roomId) {
                logger.info(QStringLiteral("player"), QStringLiteral("joined"), roomId,
                    playerId, {{QStringLiteral("name"), playerName}});
            });
        QObject::connect(&server, &Server::roomGameStarted, &app,
            [&logger](int roomId, const QString &mode) {
                logger.info(QStringLiteral("room"), QStringLiteral("game_started"),
                    roomId, QString(), {{QStringLiteral("mode"), mode}});
                Server::writeHeadlessLog("[AUTOTEST] game start");
            });
        QObject::connect(&server, &Server::roomGameOver, &app,
            [&logger](int roomId, const QString &mode, const QString &winner) {
                logger.info(QStringLiteral("room"), QStringLiteral("game_over"),
                    roomId, QString(), {{QStringLiteral("mode"), mode},
                                      {QStringLiteral("winner"), winner}});
                Server::writeHeadlessLog(QString("[AUTOTEST] game over %1").arg(winner));
            });
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &app,
            [&logger]() { logger.info(QStringLiteral("server"), QStringLiteral("stopping")); });

        if (!server.listen()) {
            logger.error(QStringLiteral("server"),
                QObject::tr("Unable to listen on the configured server endpoint"),
                -1, QString(), {{QStringLiteral("address"), Config.BindAddress},
                                {QStringLiteral("port"), Config.ServerPort},
                                {QStringLiteral("websocket_port"), Config.WebSocketPort}});
            result = 2;
        } else {
            for (const QString &message : server.startupMessages())
                logger.debug(QStringLiteral("server"), message);

            const ServerStatusSnapshot snapshot = server.statusSnapshot();
            logger.info(QStringLiteral("server"),
                QStringLiteral("Listening on %1:%2")
                    .arg(snapshot.bindAddress).arg(snapshot.port),
                -1, QString(), {{QStringLiteral("address"), snapshot.bindAddress},
                                {QStringLiteral("port"), snapshot.port},
                                {QStringLiteral("mode"), snapshot.gameMode}});
            if (snapshot.websocketPort != 0)
                logger.info(QStringLiteral("server"),
                    QStringLiteral("WebSocket listening on %1:%2")
                        .arg(snapshot.bindAddress).arg(snapshot.websocketPort),
                    -1, QString(), {{QStringLiteral("address"), snapshot.bindAddress},
                                    {QStringLiteral("websocket_port"), snapshot.websocketPort},
                                    {QStringLiteral("mode"), snapshot.gameMode}});
            console.start();
            result = app.exec();
            CrashHandler::beginShutdown();
        }
    }
#if defined(Q_OS_WIN)
    SetConsoleCtrlHandler(requestShutdown, FALSE);
#endif
    EngineBootstrap::shutdown();
    logger.info(QStringLiteral("server"), QStringLiteral("stopped"), -1, QString(),
        {{QStringLiteral("exit_code"), result}});
    return result;
}

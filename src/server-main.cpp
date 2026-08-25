#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>

#if defined(Q_OS_UNIX)
#include <csignal>
#endif

#include "core/engine-bootstrap.h"
#include "core/engine.h"
#include "core/settings.h"
#include "core/version.h"
#include "server/server-command-line.h"
#include "server/server-core.h"
#include "crashhandler.h"

namespace
{
constexpr int CliUsageExitCode = 64;

#if defined(Q_OS_UNIX)
volatile std::sig_atomic_t shutdownSignal = 0;

void requestShutdown(int signal)
{
    shutdownSignal = signal;
}
#endif

void printCommandLineError(const QString &error)
{
    QTextStream err(stderr);
    err << QCoreApplication::applicationName() << ": error: " << error << Qt::endl;
    err << "Try '" << QCoreApplication::applicationName() << " --help' for usage."
        << Qt::endl;
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

void printEffectiveConfiguration(QTextStream &out, const ServerCommandLineOptions &options)
{
    out << "server-name=" << Config.ServerName << Qt::endl;
    out << "bind-address=" << Config.BindAddress << Qt::endl;
    out << "port=" << Config.ServerPort << Qt::endl;
    out << "game-mode=" << Config.GameMode.mode_id << Qt::endl;
    out << "operation-timeout="
        << (Config.OperationNoLimit ? QStringLiteral("unlimited")
                                    : QString::number(Config.OperationTimeout))
        << Qt::endl;
    out << "ai=" << (Config.EnableAI ? QStringLiteral("on") : QStringLiteral("off"))
        << Qt::endl;
    out << "ai-delay=" << Config.OriginAIDelay << Qt::endl;
    out << "seed="
        << (options.seed ? QString::number(*options.seed) : QStringLiteral("random"))
        << Qt::endl;
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

    CrashHandler::install();
    if (options.seed) {
        QString seedError;
        if (!Server::configureGameSeed(QString::number(*options.seed), &seedError)) {
            printCommandLineError(seedError);
            return CliUsageExitCode;
        }
    }

    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        QTextStream(stderr) << error << Qt::endl;
        return 1;
    }
    // The dedicated entry point owns explicit teardown after Server/Room cleanup.
    // Avoid the legacy GUI lifetime hook deleting the engine during aboutToQuit().
    QObject::disconnect(&app, SIGNAL(aboutToQuit()), Sanguosha, SLOT(deleteLater()));

    CrashHandler::setVersion(Sanguosha->getVersionNumber().toUtf8().constData());
    if (options.listGameModes && !options.printConfig) {
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
    if (options.listGameModes)
        printAvailableGameModes(out);
    if (options.printConfig)
        printEffectiveConfiguration(out, options);
    if (options.listGameModes || options.printConfig) {
        EngineBootstrap::shutdown();
        return 0;
    }

#if defined(Q_OS_UNIX)
    std::signal(SIGINT, requestShutdown);
    std::signal(SIGTERM, requestShutdown);
#endif
    Server::isHeadlessMode = true;
    int result;
    {
        Server server(&app);
#if defined(Q_OS_UNIX)
        QTimer shutdownTimer;
        shutdownTimer.setInterval(100);
        QObject::connect(&shutdownTimer, &QTimer::timeout, &app,
            [&app, &out]() {
                if (shutdownSignal == 0)
                    return;
                out << "Shutdown requested by signal " << shutdownSignal << Qt::endl;
                app.quit();
            });
        shutdownTimer.start();
#endif
        QObject::connect(&server, &Server::logMessage, &app,
            [&out](const QString &message) { out << message << Qt::endl; });
        QObject::connect(&server, &Server::roomGameStarted, &app,
            [&out]() {
                out << "[AUTOTEST] game start" << Qt::endl;
                Server::writeHeadlessLog("[AUTOTEST] game start");
            });
        QObject::connect(&server, &Server::roomGameOver, &app,
            [&out](const QString &winner) {
                out << "[AUTOTEST] game over " << winner << Qt::endl;
                Server::writeHeadlessLog(QString("[AUTOTEST] game over %1").arg(winner));
            });

        if (!server.listen()) {
            out << QObject::tr("Unable to listen on the configured server endpoint") << Qt::endl;
            result = 2;
        } else {
            for (const QString &message : server.startupMessages())
                out << message << Qt::endl;

            result = app.exec();
            CrashHandler::beginShutdown();
        }
    }
    EngineBootstrap::shutdown();
    return result;
}

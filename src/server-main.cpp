#include <QCoreApplication>
#include <QTextStream>

#include "core/engine-bootstrap.h"
#include "core/engine.h"
#include "core/settings.h"
#include "server/server-core.h"
#include "crashhandler.h"

// 自動化測試支援:
//   --game-mode <id>  覆寫 config.ini 的 GameMode (10p / 20p / 02_1v1 / 05p ...)
//   --autotest-log <path>  以檔案輸出 [AUTOTEST] 標記 (stdout 重導向會緩衝, 檔案較可靠)
//   stdout 同時輸出 [AUTOTEST] game start / [AUTOTEST] game over <winner>
static bool parseArguments()
{
    const QStringList args = QCoreApplication::arguments();
    const int modeIdx = args.indexOf("--game-mode");
    if (modeIdx >= 0 && modeIdx + 1 < args.size()) {
        const QString modeId = args.at(modeIdx + 1);
        const GameModeStruct mode = Sanguosha->getGameMode(modeId);
        if (!mode.isValid()) {
            QTextStream(stderr) << "Unknown game mode '" << modeId << "'" << Qt::endl;
            return false;
        }
        Config.GameMode = mode;
    }
    const int logIdx = args.indexOf("--autotest-log");
    if (logIdx >= 0 && logIdx + 1 < args.size())
        Server::setHeadlessLogFile(args.at(logIdx + 1));
    return true;
}

int main(int argc, char **argv)
{
    CrashHandler::install();
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--seed") == 0) {
            qputenv("QT_HASH_SEED", "0");
            break;
        }
    }

    QCoreApplication app(argc, argv);
    const QStringList arguments = QCoreApplication::arguments();
    const int seedIdx = arguments.indexOf("--seed");
    if (seedIdx >= 0) {
        const QString candidate = seedIdx + 1 < arguments.size() ? arguments.at(seedIdx + 1) : QString();
        const QString seedText = candidate.startsWith("--") ? QString() : candidate;
        QString seedError;
        if (!Server::configureGameSeed(seedText, &seedError)) {
            QTextStream(stderr) << seedError << Qt::endl;
            return 1;
        }
    }
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        QTextStream(stderr) << error << Qt::endl;
        return 1;
    }

    // Engine 已就緒,把真實版本號補登記給 crash handler(install() 時拿不到)
    CrashHandler::setVersion(Sanguosha->getVersionNumber().toUtf8().constData());
    Config.init();
    if (!parseArguments()) {
        EngineBootstrap::shutdown();
        return 1;
    }
    Server::isHeadlessMode = true;
    Server server(&app);
    QTextStream out(stdout);
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
        out << QObject::tr("Unable to listen on the configured server port") << Qt::endl;
        EngineBootstrap::shutdown();
        return 2;
    }

    for (const QString &message : server.startupMessages())
        out << message << Qt::endl;

    const int result = app.exec();
    CrashHandler::beginShutdown(); // 正常關閉流程,退出清理階段的崩潰不再上報
    EngineBootstrap::shutdown();
    return result;
}

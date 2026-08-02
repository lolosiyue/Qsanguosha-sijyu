#include <QCoreApplication>
#include <QTextStream>

#include "core/engine-bootstrap.h"
#include "core/engine.h"
#include "core/settings.h"
#include "server/server-core.h"

// 自動化測試支援:
//   --game-mode <id>  覆寫 config.ini 的 GameMode (10p / 20p / 02_1v1 / 05p ...)
//   --autotest-log <path>  以檔案輸出 [AUTOTEST] 標記 (stdout 重導向會緩衝, 檔案較可靠)
//   stdout 同時輸出 [AUTOTEST] game start / [AUTOTEST] game over <winner>
static void parseArguments()
{
    const QStringList args = QCoreApplication::arguments();
    const int modeIdx = args.indexOf("--game-mode");
    if (modeIdx >= 0 && modeIdx + 1 < args.size()) {
        const QString modeId = args.at(modeIdx + 1);
        const GameModeStruct mode = Sanguosha->getGameMode(modeId);
        if (!mode.isValid()) {
            QTextStream(stderr) << "Unknown game mode '" << modeId << "'" << Qt::endl;
            return;
        }
        Config.GameMode = mode;
    }
    const int logIdx = args.indexOf("--autotest-log");
    if (logIdx >= 0 && logIdx + 1 < args.size())
        Server::setHeadlessLogFile(args.at(logIdx + 1));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        QTextStream(stderr) << error << Qt::endl;
        return 1;
    }

    Config.init();
    parseArguments();
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
    EngineBootstrap::shutdown();
    return result;
}

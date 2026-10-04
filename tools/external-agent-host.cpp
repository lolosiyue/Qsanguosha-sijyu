#include "external-agent-transport.h"
#include "engine-bootstrap.h"
#include "engine.h"
#include "package.h"
#include "room.h"
#include "serverplayer.h"
#include "settings.h"
#include "runtime-paths.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTimer>
#include <cstdio>

// A runnable two-seat native-role host. The bootstrap is written only to stdout,
// intended for a parent process's private pipe, never to the game logs.
int main(int argc, char **argv)
{
    QCoreApplication app(argc,argv);
    QString error;
    if (!QSanRuntimePaths::resolve(app.arguments(),&error)) { qCritical() << error; return 1; }
    Config.setValueOverrides({{"AiLegacyDirectCallbacks",QStringLiteral(
        "activate askForUseCard askForSkillInvoke askForChoice askForSuit askForKingdom "
        "askForGeneral askForDiscard askForAG askForCardChosen askForYiji askForPlayerChosen "
        "askForPlayersChosen askForCard askForNullification askForCardShow askForPindian "
        "askForSinglePeach askForGuanxing askForTriggerOrder").split(' ')},
        {"AiIsolatedCallbacks",QStringList()}});
    if (!EngineBootstrap::initialize(false,&error)) { qCritical() << error; return 1; }
    QObject::disconnect(&app,SIGNAL(aboutToQuit()),Sanguosha,SLOT(deleteLater()));
    Config.init(); Config.EnableAI = true; Config.Enable2ndGeneral = false;
    Config.EnableHegemony = false; Config.GameMode = Sanguosha->getGameMode("02p");
    Config.AIDelay = Config.OriginAIDelay = 0; Config.CountDownSeconds = 0;
    Config.BanPackages.clear();
    const QStringList allowed = {"standard","standard_cards","standard_ex_cards","maneuvering"};
    for (const auto *package : Sanguosha->getPackages())
        if (!allowed.contains(package->objectName())) Config.BanPackages << package->objectName();
    auto *room = new Room(nullptr,"02p",GameSessionConfig(20261003));
    room->setProperty("to_test","headless"); room->setNoClock(true);
    auto *seat = room->addAIPlayer(); seat->setOwner(true); room->signup(seat,"External","",true);
    auto endpoint = room->attachExternalAgent(seat,ExternalAgentEndpoint::Pause);
    if (!endpoint) { delete room; EngineBootstrap::shutdown(); return 1; }
    ExternalAgentLocalTransport transport(endpoint);
    if (!transport.listen()) { delete room; EngineBootstrap::shutdown(); return 1; }
    bool finished = false;
    QString winner;
    int callbackErrors = 0;
    QObject::connect(room,&Room::room_message,&app,[&](const QString &message) {
        if (message.contains("[AI_CALLBACK_ERROR]")) ++callbackErrors;
    });
    QObject::connect(room,&Room::game_over,&app,[&](const QString &value) {
        finished = true; winner = value;
    });
    auto *opponent = room->addAIPlayer(); room->signup(opponent,"SmartAI","",true);
    QTimer lifecycle;
    QObject::connect(&lifecycle,&QTimer::timeout,&app,[&] {
        if ((!finished && endpoint->status() != "cancelled") || !room->allGameThreadsStopped()) return;
        const bool valid = callbackErrors == 0 && (!finished || !winner.isEmpty());
        lifecycle.stop(); delete room; room = nullptr;
        transport.complete(winner);
        qInfo() << "EXTERNAL_HOST_TERMINAL winner=" << winner << "callback_errors=" << callbackErrors
                << "workers_stopped=true room_destroyed=true endpoint=" << endpoint->status();
        QTimer::singleShot(500,&app,[&,valid] { app.exit(valid ? 0 : 1); });
    });
    lifecycle.start(10);
    auto bootstrap = transport.bootstrap();
    bootstrap.insert("hostPid",QString::number(QCoreApplication::applicationPid()));
    const auto line = QByteArray("QSAN_AGENT_BOOTSTRAP ") + QJsonDocument(bootstrap).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(line.constData(),1,size_t(line.size()),stdout); std::fflush(stdout);
    room->start();
    const int result = app.exec();
    transport.close();
    if (room) { room->requestStopGameThreads(); room->wait(); delete room; }
    EngineBootstrap::shutdown();
    return result;
}

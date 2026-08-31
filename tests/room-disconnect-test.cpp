#include "engine-bootstrap.h"
#include "room-test-access.h"
#include "room.h"
#include "serverplayer.h"

#include <QCoreApplication>
#include <QDebug>

namespace {

ServerPlayer *addSeat(Room &room, const QString &objectName, const QString &state,
                      const QString &role)
{
    ServerPlayer *player = RoomTestAccess::addPlayer(room, objectName, state);
    player->setRole(role);
    player->setAlive(true);
    return player;
}

// A human who drops mid-game is expected back: the room must stay alive so the
// server keeps the registration that reconnect looks up. Robots filling the
// other seats must not make the room look abandoned.
bool humanDisconnectDoesNotEndAiGame(Room &room)
{
    ServerPlayer *human = addSeat(room, QStringLiteral("sgs1"),
        QStringLiteral("online"), QStringLiteral("lord"));
    addSeat(room, QStringLiteral("sgs2"), QStringLiteral("robot"), QStringLiteral("rebel"));
    addSeat(room, QStringLiteral("sgs3"), QStringLiteral("robot"), QStringLiteral("rebel"));

    bool gameOverEmitted = false;
    QObject::connect(&room, &Room::game_over, &room,
                     [&gameOverEmitted](const QString &) { gameOverEmitted = true; });

    RoomTestAccess::simulateDisconnect(room, human);

    if (gameOverEmitted) {
        qCritical() << "room ended the game when the only human dropped";
        return false;
    }
    if (human->getState() != QLatin1String("offline")) {
        qCritical() << "expected the dropped human to be offline, saw" << human->getState();
        return false;
    }
    return true;
}

} // namespace

int runRoomDisconnectTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }

    Room room(nullptr, QStringLiteral("03_1v2"));

    if (!humanDisconnectDoesNotEndAiGame(room))
        return 2;

    qInfo() << "room disconnect behavior passed";
    return 0;
}

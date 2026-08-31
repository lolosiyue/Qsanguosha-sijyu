#include "engine-bootstrap.h"
#include "protocol/protocol-message.h"
#include "protocol/protocol-runtime.h"
#include "protocol/session/session-payloads.h"
#include "room-test-access.h"
#include "room.h"
#include "serverplayer.h"

#include <QCoreApplication>
#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <QSemaphore>

#include <thread>

using namespace QSanProtocol;

namespace {

// Mirrors RoomNotifier::sendPacket: a plain room -> client notification.
ProtocolMessage makeNotification(const QString &detail)
{
    DiagnosticPayload diagnostic;
    diagnostic.code = QStringLiteral("ordering");
    diagnostic.message = detail;
    diagnostic.fatal = false;

    ProtocolMessage message;
    message.type = ProtocolMessageType::Notification;
    message.source = ProtocolEndpoint::Room;
    message.destination = ProtocolEndpoint::Client;
    message.command = S_COMMAND_WARN;
    message.hasPayload = true;
    message.payload = diagnostic.toVariant();
    return message;
}

// The room thread sends first, then the main thread sends. Whatever the
// dispatch mechanism, the frames must reach the socket in message_id order.
bool concurrentSendsKeepMessageIdOrder(Room &room)
{
    ServerPlayer *player = RoomTestAccess::addPlayer(room,
        QStringLiteral("ordering-player"), QStringLiteral("online"));

    QMutex mutex;
    QList<quint64> written;
    QObject::connect(player, &ServerPlayer::message_ready, player,
                     [&mutex, &written](const QByteArray &frame) {
        ProtocolMessage decoded;
        if (!ProtocolCodecRouter().decode(frame, &decoded).success)
            return;
        QMutexLocker locker(&mutex);
        written << decoded.messageId;
    });

    QSemaphore workerSent;
    quint64 workerId = 0;
    std::thread worker([player, &workerSent, &workerId]() {
        workerId = player->sendProtocolMessage(makeNotification(QStringLiteral("from-room-thread")));
        workerSent.release();
    });
    workerSent.acquire();
    worker.join();

    const quint64 mainId = player->sendProtocolMessage(
        makeNotification(QStringLiteral("from-main-thread")));

    // Let any deferred delivery land before judging the order.
    for (int i = 0; i < 8; ++i)
        QCoreApplication::processEvents();

    QMutexLocker locker(&mutex);
    if (workerId == 0 || mainId == 0 || mainId <= workerId) {
        qCritical() << "unexpected id allocation" << workerId << mainId;
        return false;
    }
    if (written.size() != 2) {
        qCritical() << "expected 2 written frames, saw" << written;
        return false;
    }
    if (written.at(0) != workerId || written.at(1) != mainId) {
        qCritical() << "frames left the server out of message_id order:" << written
                    << "expected" << workerId << mainId;
        return false;
    }
    return true;
}

} // namespace

int runOutboundOrderingTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }

    Room room(nullptr, QStringLiteral("02_1v1"));

    if (!concurrentSendsKeepMessageIdOrder(room))
        return 2;

    qInfo() << "outbound ordering behavior passed";
    return 0;
}

#include "engine-bootstrap.h"
#include "protocol/protocol-message.h"
#include "protocol/session/session-payloads.h"
#include "request-coordinator.h"
#include "room-test-access.h"
#include "room.h"
#include "serverplayer.h"

#include <QCoreApplication>
#include <QDebug>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>

using namespace QSanProtocol;

namespace {

// Room::run() is the only place that hands every player its initial SEMA_MUTEX
// permit. Anything reachable before the game starts must therefore not depend on
// that permit, or the server main thread blocks forever.
ServerPlayer *addPreGamePlayer(Room &room, const QString &objectName)
{
    ServerPlayer *player = RoomTestAccess::addPlayer(room, objectName);
    player->setState(QStringLiteral("online"));
    return player;
}

// Runs one dispatch on a worker thread so a regression reports a failed
// assertion instead of hanging the whole suite.
bool completesWithin(int timeoutMs, const std::function<void()> &work)
{
    auto done = std::make_shared<std::atomic<bool>>(false);
    std::thread worker([done, work]() {
        work();
        done->store(true);
    });

    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(timeoutMs);
    while (!done->load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    if (!done->load()) {
        worker.detach();
        return false;
    }
    worker.join();
    return true;
}

bool freshPlayerMutexIsAvailable(Room &room)
{
    ServerPlayer *player = addPreGamePlayer(room, QStringLiteral("pre-game-mutex"));
    if (!player->tryAcquireLock(ServerPlayer::SEMA_MUTEX, 0))
        return false;
    player->releaseLock(ServerPlayer::SEMA_MUTEX);
    if (!player->tryAcquireLock(ServerPlayer::SEMA_MUTEX, 0))
        return false;
    player->releaseLock(ServerPlayer::SEMA_MUTEX);
    return true;
}

bool preGameTrustDoesNotBlock(Room &room)
{
    ServerPlayer *player = addPreGamePlayer(room, QStringLiteral("pre-game-trust"));

    ProtocolMessage trust;
    trust.type = ProtocolMessageType::Notification;
    trust.source = ProtocolEndpoint::Client;
    trust.destination = ProtocolEndpoint::Room;
    trust.command = S_COMMAND_TRUST;
    trust.hasPayload = true;
    trust.payload = TrustPayload{true}.toVariant();

    if (!completesWithin(5000, [&room, player, trust]() {
            RoomTestAccess::dispatch(room, player, trust);
        })) {
        return false;
    }
    return player->getState() == QLatin1String("trust");
}

bool preGameReplyDoesNotBlock(Room &room)
{
    ServerPlayer *player = addPreGamePlayer(room, QStringLiteral("pre-game-reply"));

    ProtocolMessage reply;
    reply.type = ProtocolMessageType::Reply;
    reply.source = ProtocolEndpoint::Client;
    reply.destination = ProtocolEndpoint::Room;
    reply.command = S_COMMAND_RESPONSE_CARD;
    reply.replyTo = 1;
    reply.hasPayload = false;

    return completesWithin(5000, [&room, player, reply]() {
        RoomTestAccess::dispatch(room, player, reply);
    });
}

} // namespace

int runPreGameLockTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }

    Room room(nullptr, QStringLiteral("02_1v1"));

    // Each case is independent, so report every failure in one run instead of
    // stopping at the first one.
    int failure = 0;
    if (!freshPlayerMutexIsAvailable(room)) {
        qCritical() << "fresh player SEMA_MUTEX is not available";
        failure = failure != 0 ? failure : 2;
    }
    if (!preGameTrustDoesNotBlock(room)) {
        qCritical() << "pre-game trust command blocked";
        failure = failure != 0 ? failure : 3;
    }
    if (!preGameReplyDoesNotBlock(room)) {
        qCritical() << "pre-game client reply blocked";
        failure = failure != 0 ? failure : 4;
    }
    if (failure != 0)
        return failure;

    qInfo() << "pre-game lock behavior passed";
    return 0;
}

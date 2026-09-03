#include "card-lifetime-manager.h"
#include "card-lifetime-test-check.h"
#include "card.h"
#include "engine-bootstrap.h"
#include "room.h"
#include "room-runtime.h"
#include "structs.h"

#include <QCoreApplication>
#include <QEvent>
#include <QThread>

namespace {

int runNormal(RoomRuntime &runtime)
{
    QThread worker;
    QObject context;
    QThread *ownerThread = QThread::currentThread();
    context.moveToThread(&worker);
    worker.start();
    QPointer<Card> workerCard;
    QPointer<Card> workerEventCard;
    std::shared_ptr<const CardLifetimeToken> workerToken;
    QMetaObject::invokeMethod(&context, [&] {
        const void *previousDomain = CardLifetimeManager::setCurrentDomain(&runtime);
        auto *card = new DummyCard;
        workerCard = card;
        CardLifetimeManager &manager = globalCardLifetimeManager();
        workerToken = manager.observeCard(card);
        CARD_LIFETIME_CHECK(workerToken && manager.retainWrapper(workerToken));
        CARD_LIFETIME_CHECK(manager.requestNativeDelete(workerToken));
        workerEventCard = new DummyCard;
        DamageStruct eventSource(workerEventCard, nullptr, nullptr);
        DamageStruct eventPayload(eventSource);
        runtime.finalizeWorker();
        CARD_LIFETIME_CHECK(workerEventCard.isNull());
        CardLifetimeManager::setCurrentDomain(previousDomain);
        context.moveToThread(ownerThread);
    }, Qt::BlockingQueuedConnection);
    worker.quit();
    CARD_LIFETIME_CHECK(worker.wait(5000));
    CARD_LIFETIME_CHECK(workerCard.isNull());
    CARD_LIFETIME_CHECK(workerEventCard.isNull());
    CARD_LIFETIME_CHECK(workerToken && workerToken->state == CardLifetimeState::Dead);
    CARD_LIFETIME_CHECK(globalCardLifetimeManager().releaseWrapper(workerToken));

    QPointer<Card> ownerCard = new DummyCard;
    CardLifetimeManager &manager = globalCardLifetimeManager();
    const auto ownerToken = manager.observeCard(ownerCard.data());
    CARD_LIFETIME_CHECK(ownerToken && manager.requestNativeDelete(ownerToken));
    CARD_LIFETIME_CHECK(runtime.shutdownState() == RoomRuntime::ShutdownState::Running);
    runtime.shutdownFinal();
    CARD_LIFETIME_CHECK(runtime.shutdownState() == RoomRuntime::ShutdownState::Closed);
    CARD_LIFETIME_CHECK(ownerCard.isNull());
    CARD_LIFETIME_CHECK(ownerToken->state == CardLifetimeState::Dead);
    runtime.shutdownFinal();
    CARD_LIFETIME_CHECK(runtime.shutdownState() == RoomRuntime::ShutdownState::Closed);
    return 0;
}

int runOverlappingRooms(RoomRuntime &runtime)
{
    CardLifetimeManager &manager = globalCardLifetimeManager();
    auto *otherRoom = new Room(nullptr, QStringLiteral("03_1v2"));
    QPointer<Room> otherRoomGuard(otherRoom);
    RoomRuntime *otherRuntime = otherRoom->roomRuntime();
    const quint64 baselineUnknown = manager.gauge().unknown_unclaimed;

    const void *previousDomain = CardLifetimeManager::setCurrentDomain(&runtime);
    QPointer<Card> outerUnclaimed = new DummyCard;
    const auto outerToken = manager.observeCard(outerUnclaimed.data());
    manager.recordOwningFactoryResult(outerToken);
    CardLifetimeManager::setCurrentDomain(previousDomain);
    CARD_LIFETIME_CHECK(outerToken);
    CARD_LIFETIME_CHECK(manager.gauge().unknown_unclaimed == baselineUnknown + 1);

    otherRuntime->shutdownFinal();
    CARD_LIFETIME_CHECK(otherRuntime->shutdownState() == RoomRuntime::ShutdownState::Closed);
    CARD_LIFETIME_CHECK(otherRoomGuard);
    CARD_LIFETIME_CHECK(manager.isLive(outerToken));

    CARD_LIFETIME_CHECK(manager.requestNativeDelete(outerToken));
    otherRoom->deleteLater();
    runtime.shutdownFinal();
    CARD_LIFETIME_CHECK(runtime.shutdownState() == RoomRuntime::ShutdownState::Closed);
    CARD_LIFETIME_CHECK(otherRoomGuard);
    CARD_LIFETIME_CHECK(outerUnclaimed.isNull());
    CARD_LIFETIME_CHECK(!manager.isLive(outerToken));
    CARD_LIFETIME_CHECK(manager.gauge().unknown_unclaimed == baselineUnknown);

    QCoreApplication::sendPostedEvents(otherRoomGuard.data(), QEvent::DeferredDelete);
    CARD_LIFETIME_CHECK(otherRoomGuard.isNull());
    std::fprintf(stdout, "CARD_LIFETIME_OVERLAP PASS\n");
    return 0;
}

int runPendingWorkerCard(RoomRuntime &runtime)
{
    QThread worker;
    worker.start();
    auto *card = new DummyCard;
    card->moveToThread(&worker);
    CardLifetimeManager &manager = globalCardLifetimeManager();
    const auto token = manager.observeCard(card);
    CARD_LIFETIME_CHECK(token && manager.requestNativeDelete(token));
    runtime.shutdownFinal();
    return 99;
}

int runNonzeroLease(RoomRuntime &runtime)
{
    auto *card = new DummyCard;
    CardLifetimeManager &manager = globalCardLifetimeManager();
    const auto token = manager.observeCard(card);
    CARD_LIFETIME_CHECK(token && manager.retainNativeLease(token));
    CARD_LIFETIME_CHECK(manager.requestNativeDelete(token));
    runtime.shutdownFinal();
    return 98;
}

int runNonzeroReservation(RoomRuntime &runtime)
{
    auto *card = new DummyCard;
    CardLifetimeManager &manager = globalCardLifetimeManager();
    const auto token = manager.observeCard(card);
    CARD_LIFETIME_CHECK(token && manager.reserveAdoption(token));
    CARD_LIFETIME_CHECK(manager.requestNativeDelete(token));
    runtime.shutdownFinal();
    return 97;
}

int runNonzeroLuaPin(RoomRuntime &runtime)
{
    CardLifetimeManager &manager = globalCardLifetimeManager();
    manager.enterLuaPin();
    runtime.shutdownFinal();
    return 96;
}

}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QString error;
    if (!EngineBootstrap::initialize(false, &error))
        return 2;

    Room room(nullptr, QStringLiteral("03_1v2"));
    RoomRuntime &runtime = *room.roomRuntime();
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QLatin1String("worker"))
        return runPendingWorkerCard(runtime);
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QLatin1String("lease"))
        return runNonzeroLease(runtime);
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QLatin1String("reservation"))
        return runNonzeroReservation(runtime);
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QLatin1String("lua-pin"))
        return runNonzeroLuaPin(runtime);
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QLatin1String("overlap"))
        return runOverlappingRooms(runtime);
    return runNormal(runtime);
}

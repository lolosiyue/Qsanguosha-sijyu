#include "card-lifetime-manager.h"
#include "card-lifetime-test-check.h"
#include "card.h"
#include "engine-bootstrap.h"
#include "room.h"
#include "room-runtime.h"
#include "structs.h"

#include <QCoreApplication>
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

    CARD_LIFETIME_CHECK(runtime.shutdownState() == RoomRuntime::ShutdownState::Running);
    runtime.shutdownFinal();
    CARD_LIFETIME_CHECK(runtime.shutdownState() == RoomRuntime::ShutdownState::Closed);
    runtime.shutdownFinal();
    CARD_LIFETIME_CHECK(runtime.shutdownState() == RoomRuntime::ShutdownState::Closed);
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
    return runNormal(runtime);
}

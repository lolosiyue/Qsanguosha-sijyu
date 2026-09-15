#include "card-lifetime-manager.h"
#include "card-lifetime-test-check.h"
#include "card.h"
#include "engine-bootstrap.h"
#include "lua-runtime.h"
#include "room.h"
#include "room-runtime.h"
#include "structs.h"
#include "lua.hpp"

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

struct LuaCallbackUnwindMarker
{
    bool *unwound;
    ~LuaCallbackUnwindMarker() { *unwound = true; }
};

int throwGameFinishedFromLua(lua_State *state)
{
    auto *unwound = static_cast<bool *>(lua_touserdata(state, lua_upvalueindex(1)));
    LuaCallbackUnwindMarker marker{unwound};
    throw GameFinished;
}

int callLuaThatThrowsGameFinished(lua_State *state)
{
    lua_pushlightuserdata(state, lua_touserdata(state, lua_upvalueindex(1)));
    lua_pushcclosure(state, throwGameFinishedFromLua, 1);
    return LuaRuntime::protectedCall(state, 0, 0, 0);
}

int runLuaExceptionUnwind(RoomRuntime &roomRuntime)
{
    CardLifetimeManager &manager = globalCardLifetimeManager();
    LuaRuntime &runtime = roomRuntime.lua();
    QThread worker;
    QObject context;
    QThread *ownerThread = QThread::currentThread();
    context.moveToThread(&worker);
    worker.start();
    QMetaObject::invokeMethod(&context, [&] {
        const void *previousDomain = CardLifetimeManager::setCurrentDomain(&roomRuntime);
        QString error;
        CARD_LIFETIME_CHECK(runtime.initialize(&error));
        bool caughtGameFinished = false;
        bool callbackFrameUnwound = false;
        try {
            LuaRuntime::Binding binding(runtime);
            // C++ -> Lua -> C++ -> Lua: mimic a nested callback that exits the game.
            lua_pushlightuserdata(runtime.rawState(), &callbackFrameUnwound);
            lua_pushcclosure(runtime.rawState(), callLuaThatThrowsGameFinished, 1);
            LuaRuntime::protectedCall(runtime.rawState(), 0, 0, 0);
        } catch (TriggerEvent event) {
            caughtGameFinished = event == GameFinished;
        }

        const CardLifetimeGauge domainGauge = manager.gaugeForDomain(&roomRuntime);
        const CardLifetimeGauge runtimeGauge = manager.gaugeForRuntime(&roomRuntime, &runtime,
            runtime.generation(), runtime.rawState());
        const int aiDepth = roomRuntime.ai().lua().invocationDepth();
        std::fprintf(stdout, "lua_exception_unwind caught=%d callback_unwound=%d depth=%d ai_depth=%d domain_pins=%llu runtime_pins=%llu\n",
            int(caughtGameFinished), int(callbackFrameUnwound), runtime.invocationDepth(), aiDepth,
            static_cast<unsigned long long>(domainGauge.lua_pins),
            static_cast<unsigned long long>(runtimeGauge.lua_pins));
        std::fflush(stdout);
        CARD_LIFETIME_CHECK(caughtGameFinished);
        CARD_LIFETIME_CHECK(callbackFrameUnwound);
        CARD_LIFETIME_CHECK(runtime.invocationDepth() == 0);
        CARD_LIFETIME_CHECK(aiDepth == 0);
        CARD_LIFETIME_CHECK(domainGauge.lua_pins == 0);
        CARD_LIFETIME_CHECK(runtimeGauge.lua_pins == 0);
        CARD_LIFETIME_CHECK(manager.gaugeForRuntime(&roomRuntime, &roomRuntime.ai().lua(),
            roomRuntime.ai().lua().generation(), roomRuntime.ai().lua().rawState()).lua_pins == 0);

        // Close both registered Room Lua runtimes on their worker before domain finalization.
        roomRuntime.finalizeWorker();
        CARD_LIFETIME_CHECK(runtime.isClosed());
        CARD_LIFETIME_CHECK(roomRuntime.ai().lua().isClosed());
        CardLifetimeManager::setCurrentDomain(previousDomain);
        context.moveToThread(ownerThread);
    }, Qt::BlockingQueuedConnection);
    worker.quit();
    CARD_LIFETIME_CHECK(worker.wait(5000));
    roomRuntime.shutdownFinal();
    CARD_LIFETIME_CHECK(roomRuntime.shutdownState() == RoomRuntime::ShutdownState::Closed);
    std::fprintf(stdout, "CARD_LIFETIME_LUA_EXCEPTION_UNWIND PASS\n");
    return 0;
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
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QLatin1String("lua-exception-unwind"))
        return runLuaExceptionUnwind(runtime);
    return runNormal(runtime);
}

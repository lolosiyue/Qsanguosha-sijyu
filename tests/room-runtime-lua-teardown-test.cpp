// Regression coverage for the room-teardown use-after-free that killed the
// dedicated server after a finished game (05p seed 7777 reproduced it 3/3).
//
// A Lua-owned `QVariant` can box a `CardUseStruct` whose `m_ownedCard` is a
// co-owner of a transient Card, and those SWIG finalizers only run at
// `lua_close()`. While `lua_close()` happened in `RoomRuntime::shutdownFinal()`
// the Card had already been retired by the worker's `finalizeWorkerDomain()`,
// so the `QSharedPointer` deleter called `Card::deleteLater()` on freed memory
// and `observeCard()` crashed inside `QObject::connect`. `finalizeWorker()`
// therefore has to close the room's Lua runtimes *before* it retires anything,
// so Lua hands ownership back while the Cards are still alive.

#include "ai-runtime.h"
#include "card-lifetime-manager.h"
#include "engine.h"
#include "engine-bootstrap.h"
#include "game-rng.h"
#include "lua-runtime.h"
#include "room.h"
#include "room-runtime.h"
#include "settings.h"
#include "structs.h"

#include "lua.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QThread>

#include <memory>

namespace {

const char *const kSeedTag = "qsan_teardown_seed";

// Hands the room's own Lua state a QVariant that boxes a CardUseStruct
// co-owning a transient SkillCard, then drops every C++ reference to it. This
// is the shape the crashing production skill card had: `data:toCardUse()`
// copies the struct into Lua, and the copy keeps the owning QSharedPointer
// alive until `lua_close()`.
QByteArray parkLuaHeldOwnedCardUse(Room &room)
{
    GameRng::Binding rngBinding(room.roomRuntime()->rng());
    LuaRuntime::Binding luaBinding(room.roomRuntime()->lua());
    EngineRuntimeContextScope contextScope(*Sanguosha, &room);
    lua_State *L = room.getLuaState();
    if (!L)
        return QByteArrayLiteral("no-lua-state");

    Card *transient = Sanguosha->cloneSkillCard(QStringLiteral("ZhihengCard"));
    if (!transient)
        return QByteArrayLiteral("no-clone");
    {
        CardUseStruct use;
        use.setOwnedCard(transient);
        room.setTag(QString::fromLatin1(kSeedTag), QVariant::fromValue(use));
    }

    // Lua takes its own copy; the room tag is the only other holder and it goes
    // away right below, leaving the Lua state as the last owner.
    static const char *script =
        "local room = sgs.Sanguosha:currentRoom()\n"
        "if not room then return 'no-room' end\n"
        "local use = room:getTag('qsan_teardown_seed'):toCardUse()\n"
        "qsan_teardown_holder = sgs.QVariant()\n"
        "qsan_teardown_holder:setValue(use)\n"
        "qsan_teardown_use = use\n"
        "return 'ok'\n";
    if (luaL_dostring(L, script) != 0) {
        const QByteArray message(lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown lua error");
        lua_pop(L, 1);
        return message;
    }
    const QByteArray result(lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
    lua_pop(L, 1);
    room.removeTag(QString::fromLatin1(kSeedTag));
    return result;
}

} // namespace

int runRoomRuntimeLuaTeardownTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "Engine bootstrap failed:" << error;
        return 1;
    }

    const GameSessionConfig sessionConfig(Q_UINT64_C(0x20260902a1b2c3d4));
    std::unique_ptr<Room> room(new Room(nullptr, QStringLiteral("02_1v1"), sessionConfig));
    RoomRuntime *runtime = room->roomRuntime();
    if (!runtime || !runtime->lua().rawState() || !runtime->ai().lua().rawState()) {
        qCritical() << "The room did not come up with its own Lua runtimes";
        return 2;
    }

    // Everything a skill does happens on the room worker thread, and
    // finalizeWorkerDomain() only retires Cards with that affinity, so the
    // fixture has to run there as well.
    QByteArray parked;
    {
        std::unique_ptr<QThread> worker(QThread::create([&] {
            parked = parkLuaHeldOwnedCardUse(*room);
            if (parked != QByteArrayLiteral("ok"))
                return;
            // The scope guard at the end of RoomThread::run() calls this; it is
            // the point where the domain's transient Cards are retired.
            room->roomRuntime()->finalizeWorker();
        }));
        worker->start();
        if (!worker->wait(120000)) {
            qCritical() << "The simulated room worker did not finish";
            return 7;
        }
    }
    if (parked != QByteArrayLiteral("ok")) {
        qCritical() << "Unable to park a Lua-held CardUseStruct:" << parked;
        return 3;
    }

    if (!runtime->lua().isClosed()) {
        qCritical() << "finalizeWorker() retired the domain with the room Lua state still open; "
                       "Lua-held CardUseStructs would outlive their Cards";
        return 4;
    }
    if (!runtime->ai().lua().isClosed()) {
        qCritical() << "finalizeWorker() retired the domain with the AI Lua state still open";
        return 5;
    }

    // Pre-fix this is where the process died: shutdownFinal() ran lua_close()
    // and the parked variant's deleter touched an already-freed Card. It also
    // refuses to reach Closed unless the room's domain gauge is back to zero.
    runtime->shutdownFinal();
    if (runtime->shutdownState() != RoomRuntime::ShutdownState::Closed) {
        qCritical() << "Room runtime shutdown did not reach Closed:"
                    << int(runtime->shutdownState());
        return 6;
    }
    room.reset();

    EngineBootstrap::shutdown();
    qInfo() << "room runtime lua teardown passed";
    return 0;
}

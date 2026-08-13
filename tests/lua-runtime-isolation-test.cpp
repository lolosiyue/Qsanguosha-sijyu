#include "game-rng.h"
#include "lua-runtime.h"

#include "lua.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QSemaphore>
#include <QThread>

static bool statesAndGlobalsAreIsolated()
{
    LuaRuntime first(LuaRuntime::Game);
    LuaRuntime second(LuaRuntime::Game);
    if (!first.initialize() || !second.initialize() || first.rawState() == second.rawState())
        return false;

    {
        LuaRuntime::Binding binding(first);
        lua_pushinteger(first.state(), 41);
        lua_setglobal(first.state(), "room_value");
    }
    {
        LuaRuntime::Binding binding(second);
        lua_getglobal(second.state(), "room_value");
        const bool isolated = lua_isnil(second.state(), -1);
        lua_pop(second.state(), 1);
        if (!isolated)
            return false;
    }
    return true;
}

static bool callbackRejectsAnotherRuntime()
{
    LuaRuntime first(LuaRuntime::Game);
    LuaRuntime second(LuaRuntime::Game);
    if (!first.initialize() || !second.initialize())
        return false;

    LuaCallbackRef callback;
    {
        LuaRuntime::Binding binding(first);
        if (luaL_dostring(first.state(), "return function(value) return value + 1 end") != 0)
            return false;
        callback = LuaCallbackRef(first.state(), luaL_ref(first.state(), LUA_REGISTRYINDEX));
    }

    {
        LuaRuntime::Binding binding(second);
        if (callback.push(second.state()) || !lua_isnil(second.state(), -1))
            return false;
        lua_pop(second.state(), 1);
    }

    {
        LuaRuntime::Binding binding(first);
        if (!callback.push(first.state()))
            return false;
        lua_pushinteger(first.state(), 41);
        if (lua_pcall(first.state(), 1, 1, 0) != 0)
            return false;
        const bool valid = lua_tointeger(first.state(), -1) == 42;
        lua_pop(first.state(), 1);
        return valid;
    }
}

static bool ownerCanMoveBetweenRoomThreads()
{
    LuaRuntime runtime(LuaRuntime::Game);
    if (!runtime.initialize())
        return false;

    bool workerOwned = false;
    QThread *thread = QThread::create([&runtime, &workerOwned]() {
        LuaRuntime::Binding binding(runtime);
        workerOwned = runtime.state() != nullptr && LuaRuntime::currentState() == runtime.state();
    });
    thread->start();
    thread->wait();
    delete thread;

    if (!workerOwned || runtime.state() != nullptr)
        return false;
    LuaRuntime::Binding binding(runtime);
    return runtime.state() != nullptr;
}

static bool gameRngInstancesDoNotShareSequenceState()
{
    GameRng first;
    GameRng second;
    first.seed(20260813);
    second.seed(20260813);

    const int firstA = first.bounded(1000000);
    const int firstB = first.bounded(1000000);
    const int secondA = second.bounded(1000000);
    const int secondB = second.bounded(1000000);
    return firstA == secondA && firstB == secondB;
}

static bool oneRuntimeSerializesThreadHandoffs()
{
    LuaRuntime runtime(LuaRuntime::Game);
    if (!runtime.initialize())
        return false;

    QSemaphore entered;
    QSemaphore release;
    QSemaphore acquired;
    QThread *first = QThread::create([&]() {
        LuaRuntime::Binding binding(runtime);
        entered.release();
        release.acquire();
    });
    QThread *second = QThread::create([&]() {
        LuaRuntime::Binding binding(runtime);
        acquired.release();
    });
    first->start();
    entered.acquire();
    second->start();
    const bool enteredConcurrently = acquired.tryAcquire(1, 250);
    release.release();
    first->wait();
    second->wait();
    const bool acquiredAfterRelease = acquired.tryAcquire(1, 1000);
    delete first;
    delete second;
    return !enteredConcurrently && acquiredAfterRelease;
}

static bool hardMemoryLimitRejectsOversizedAllocation()
{
    LuaRuntime runtime(LuaRuntime::Auxiliary);
    const size_t hardLimit = 4u * 1024u * 1024u;
    runtime.setMemoryLimits(2u * 1024u * 1024u, hardLimit);
    if (!runtime.initialize())
        return false;
    LuaRuntime::Binding binding(runtime);
    const bool rejected = luaL_dostring(runtime.state(),
        "local value = string.rep('x', 8 * 1024 * 1024); return #value") != 0;
    lua_pop(runtime.state(), 1);
    return rejected && runtime.memoryBytes() <= hardLimit;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (!statesAndGlobalsAreIsolated()) {
        qCritical() << "Lua runtimes shared state or globals";
        return 1;
    }
    if (!callbackRejectsAnotherRuntime()) {
        qCritical() << "Lua callback crossed runtime identity";
        return 2;
    }
    if (!ownerCanMoveBetweenRoomThreads()) {
        qCritical() << "Lua runtime owner handoff failed";
        return 3;
    }
    if (!gameRngInstancesDoNotShareSequenceState()) {
        qCritical() << "Game RNG instances contaminated each other";
        return 4;
    }
    if (!oneRuntimeSerializesThreadHandoffs()) {
        qCritical() << "One Lua runtime executed on two threads concurrently";
        return 5;
    }
    if (!hardMemoryLimitRejectsOversizedAllocation()) {
        qCritical() << "Lua runtime hard memory limit was not enforced";
        return 6;
    }
    return 0;
}

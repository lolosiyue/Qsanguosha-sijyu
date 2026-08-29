#include "game-rng.h"
#include "lua-runtime.h"
#include "util.h"

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
    GameRng differentHighBits;
    const quint64 seed = Q_UINT64_C(0x12345678abcdef01);
    first.seed(seed);
    second.seed(seed);
    differentHighBits.seed(Q_UINT64_C(0x87654321abcdef01));

    QList<int> firstSequence;
    QList<int> secondSequence;
    QList<int> differentSequence;
    for (int i = 0; i < 4; ++i) {
        firstSequence << first.bounded(1000000);
        secondSequence << second.bounded(1000000);
        differentSequence << differentHighBits.bounded(1000000);
    }
    return firstSequence == secondSequence && firstSequence != differentSequence;
}

static bool gameShuffleIsDeterministicAndPreservesElements()
{
    GameRng firstRng;
    GameRng secondRng;
    firstRng.seed(Q_UINT64_C(0x12345678abcdef01));
    secondRng.seed(Q_UINT64_C(0x12345678abcdef01));

    QList<int> first{0, 1, 2, 3, 4, 5};
    QList<int> second = first;
    {
        GameRng::Binding binding(firstRng);
        qsanShuffle(first);
    }
    {
        GameRng::Binding binding(secondRng);
        qsanShuffle(second);
    }

    QList<int> sorted = first;
    std::sort(sorted.begin(), sorted.end());
    QList<int> empty;
    {
        GameRng::Binding binding(firstRng);
        qsanShuffle(empty);
    }
    return first == second
        && sorted == QList<int>({0, 1, 2, 3, 4, 5})
        && empty.isEmpty();
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

int runLuaRuntimeIsolationTests()
{
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
        qCritical() << "Game RNG instances shared state or truncated the 64-bit seed";
        return 4;
    }
    if (!gameShuffleIsDeterministicAndPreservesElements()) {
        qCritical() << "Game shuffle was nondeterministic or changed its input set";
        return 5;
    }
    if (!oneRuntimeSerializesThreadHandoffs()) {
        qCritical() << "One Lua runtime executed on two threads concurrently";
        return 6;
    }
    if (!hardMemoryLimitRejectsOversizedAllocation()) {
        qCritical() << "Lua runtime hard memory limit was not enforced";
        return 7;
    }
    return 0;
}

#include "lua-runtime.h"

#include "lua.hpp"
#include "util.h"

#include <QAtomicInteger>
#include <QDebug>
#include <QHash>
#include <QMutexLocker>

#include <algorithm>
#include <cstdlib>

namespace {

QHash<lua_State *, LuaRuntime *> &runtimeRegistry()
{
    static QHash<lua_State *, LuaRuntime *> registry;
    return registry;
}

QAtomicInteger<quint64> &nextGeneration()
{
    static QAtomicInteger<quint64> generation(0);
    return generation;
}

thread_local LuaRuntime *currentRuntime = nullptr;

}

LuaRuntime::LuaRuntime(Kind kind)
    : m_kind(kind), m_state(nullptr), m_generation(0), m_owner(nullptr)
{
}

LuaRuntime::~LuaRuntime()
{
    shutdown();
}

void LuaRuntime::shutdown()
{
    if (!m_state)
        return;

    {
        QMutexLocker locker(&registryMutex());
        runtimeRegistry().remove(m_state);
    }
    if (currentRuntime == this)
        currentRuntime = nullptr;
    lua_close(m_state);
    m_state = nullptr;
}

bool LuaRuntime::initialize(QString *error)
{
    if (m_state)
        return true;

    m_state = m_memory.hardLimit > 0
        ? CreateLuaState(&LuaRuntime::memoryAllocator, &m_memory)
        : CreateLuaState();
    if (!m_state) {
        if (error)
            *error = QStringLiteral("luaL_newstate failed");
        return false;
    }

    m_generation = nextGeneration().fetchAndAddRelaxed(1) + 1;
    m_owner = QThread::currentThread();
    {
        QMutexLocker locker(&registryMutex());
        runtimeRegistry().insert(m_state, this);
    }
    return true;
}

void LuaRuntime::setMemoryLimits(size_t softLimit, size_t hardLimit)
{
    Q_ASSERT(!m_state);
    Q_ASSERT(hardLimit == 0 || softLimit <= hardLimit);
    m_memory.softLimit = softLimit;
    m_memory.hardLimit = hardLimit;
}

bool LuaRuntime::loadScript(const QString &path, QString *error)
{
    lua_State *L = state();
    if (!L) {
        if (error)
            *error = QStringLiteral("Lua runtime is not owned by the current thread");
        return false;
    }

    if (luaL_loadfile(L, path.toUtf8().constData()) != 0) {
        if (error)
            *error = QString::fromUtf8(lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    if (lua_pcall(L, 0, LUA_MULTRET, 0) != 0) {
        if (error)
            *error = QString::fromUtf8(lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    return true;
}

bool LuaRuntime::addPackagePath(const QString &pattern, QString *error)
{
    lua_State *L = state();
    if (!L) {
        if (error)
            *error = QStringLiteral("Lua runtime is not owned by the current thread");
        return false;
    }

    lua_getglobal(L, "package");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        if (error)
            *error = QStringLiteral("Lua package table is unavailable");
        return false;
    }
    lua_getfield(L, -1, "path");
    QByteArray path = lua_isstring(L, -1) ? QByteArray(lua_tostring(L, -1)) : QByteArray();
    lua_pop(L, 1);
    const QByteArray encodedPattern = pattern.toUtf8();
    if (!path.split(';').contains(encodedPattern)) {
        if (!path.isEmpty())
            path.append(';');
        path.append(encodedPattern);
        lua_pushlstring(L, path.constData(), path.size());
        lua_setfield(L, -2, "path");
    }
    lua_pop(L, 1);
    return true;
}

lua_State *LuaRuntime::state() const
{
    if (!m_state || !isCurrentThreadOwner() || currentRuntime != this)
        return nullptr;
    return m_state;
}

void LuaRuntime::adoptCurrentThread()
{
    m_owner = QThread::currentThread();
}

bool LuaRuntime::isCurrentThreadOwner() const
{
    return m_owner == QThread::currentThread();
}

LuaRuntime *LuaRuntime::fromState(lua_State *state)
{
    QMutexLocker locker(&registryMutex());
    return runtimeRegistry().value(state, nullptr);
}

LuaRuntime *LuaRuntime::current()
{
    return currentRuntime;
}

lua_State *LuaRuntime::currentState()
{
    return currentRuntime ? currentRuntime->state() : nullptr;
}

void LuaRuntime::setCurrentForThread(LuaRuntime *runtime)
{
    currentRuntime = runtime;
}

LuaRuntime::Binding::Binding(LuaRuntime &runtime, bool adoptOwner)
    : m_runtime(&runtime), m_previous(currentRuntime)
{
    m_runtime->m_executionMutex.lock();
    if (adoptOwner)
        runtime.adoptCurrentThread();
    currentRuntime = &runtime;
}

LuaRuntime::Binding::~Binding()
{
    currentRuntime = m_previous;
    m_runtime->m_executionMutex.unlock();
}

QMutex &LuaRuntime::registryMutex()
{
    static QMutex mutex;
    return mutex;
}

void *LuaRuntime::memoryAllocator(void *userData, void *pointer,
                                  size_t oldSize, size_t newSize)
{
    MemoryState *memory = static_cast<MemoryState *>(userData);
    if (newSize == 0) {
        if (pointer) {
            memory->current -= std::min(memory->current, oldSize);
            std::free(pointer);
        }
        return nullptr;
    }

    const size_t accountedOldSize = pointer ? oldSize : 0;
    const size_t retained = memory->current - std::min(memory->current, accountedOldSize);
    if (memory->hardLimit > 0 && newSize > memory->hardLimit - retained)
        return nullptr;

    void *resized = std::realloc(pointer, newSize);
    if (!resized)
        return nullptr;
    memory->current = retained + newSize;
    memory->peak = std::max(memory->peak, memory->current);
    return resized;
}

LuaCallbackRef::LuaCallbackRef()
    : m_state(nullptr), m_generation(0), m_registryRef(LUA_NOREF)
{
}

LuaCallbackRef::LuaCallbackRef(int nullValue)
    : m_state(nullptr), m_generation(0), m_registryRef(LUA_NOREF)
{
    Q_ASSERT(nullValue == 0 || nullValue == LUA_NOREF || nullValue == LUA_REFNIL);
}

LuaCallbackRef::LuaCallbackRef(lua_State *state, int registryRef)
    : m_state(state), m_generation(0), m_registryRef(registryRef)
{
    LuaRuntime *runtime = LuaRuntime::fromState(state);
    if (runtime)
        m_generation = runtime->generation();
}

LuaCallbackRef &LuaCallbackRef::operator=(int nullValue)
{
    Q_ASSERT(nullValue == 0 || nullValue == LUA_NOREF || nullValue == LUA_REFNIL);
    m_state = nullptr;
    m_generation = 0;
    m_registryRef = LUA_NOREF;
    return *this;
}

bool LuaCallbackRef::isValid() const
{
    return m_state && m_registryRef != LUA_NOREF && m_registryRef != LUA_REFNIL;
}

bool LuaCallbackRef::operator==(int nullValue) const
{
    Q_ASSERT(nullValue == 0);
    return !isValid();
}

bool LuaCallbackRef::push(lua_State *target) const
{
    if (!target || !isValid()) {
        if (target)
            lua_pushnil(target);
        return false;
    }

    LuaRuntime *runtime = LuaRuntime::fromState(target);
    if (target != m_state || !runtime || runtime->generation() != m_generation
        || !runtime->isCurrentThreadOwner()) {
        qWarning() << "Rejected Lua callback from another runtime or owner thread";
        lua_pushnil(target);
        return false;
    }

    lua_rawgeti(target, LUA_REGISTRYINDEX, m_registryRef);
    return lua_isfunction(target, -1);
}

lua_State *LuaCallbackRef::state() const
{
    LuaRuntime *runtime = LuaRuntime::fromState(m_state);
    if (!runtime || runtime->generation() != m_generation)
        return nullptr;
    return runtime->state();
}

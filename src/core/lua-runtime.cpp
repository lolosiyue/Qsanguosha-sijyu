#include "lua-runtime.h"
#include "card-lifetime-manager.h"

#include "lua.hpp"
#include "util.h"

#include <QAtomicInteger>
#include <QCoreApplication>
#include <QDebug>
#include <QEvent>
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
    : m_kind(kind), m_state(nullptr), m_generation(0), m_seed(0),
      m_hasSeed(false), m_owner(nullptr)
{
}

LuaRuntime::~LuaRuntime()
{
    shutdown();
}

void LuaRuntime::shutdown()
{
    QMutexLocker executionLock(&m_executionMutex);
    Lifecycle expected = Lifecycle::Running;
    if (!m_lifecycle.compare_exchange_strong(expected, Lifecycle::Closing,
                                              std::memory_order_acq_rel))
        return;
    if (!m_state) {
        m_lifecycle.store(Lifecycle::Closed, std::memory_order_release);
        return;
    }
    if (m_invocationDepth.load(std::memory_order_acquire) != 0) {
        m_lifecycle.store(Lifecycle::Running, std::memory_order_release);
        return;
    }

    LuaRuntime *previousRuntime = currentRuntime;
    const CardLifetimeRuntimeContext previousContext =
        CardLifetimeManager::setCurrentRuntimeContext(lifetimeDomain(), this,
                                                       m_generation, m_state);
    currentRuntime = this;
    CardLifetimeManager &manager = globalCardLifetimeManager();
    manager.drain();
    if (QCoreApplication::instance())
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    {
        QMutexLocker locker(&registryMutex());
        runtimeRegistry().remove(m_state);
    }
    lua_State *closingState = m_state;
    lua_close(closingState);
    m_state = nullptr;
    manager.releaseWrapperBindings(lifetimeDomain(), this, m_generation, closingState);
    manager.drain();
    if (QCoreApplication::instance())
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    manager.drain();
    manager.unregisterRuntimeDomain(lifetimeDomain(), this, m_generation, closingState);
    m_lifecycle.store(Lifecycle::Closed, std::memory_order_release);
    currentRuntime = previousRuntime;
    CardLifetimeManager::setCurrentRuntimeContext(previousContext.domain,
                                                  previousContext.identity,
                                                  previousContext.generation,
                                                  previousContext.state);
}

bool LuaRuntime::initialize(QString *error)
{
    QMutexLocker executionLock(&m_executionMutex);
    if (m_state)
        return true;

    Lifecycle lifecycle = m_lifecycle.load(std::memory_order_acquire);
    if (lifecycle == Lifecycle::Closing)
        return false;
    if (lifecycle == Lifecycle::Closed)
        m_lifecycle.store(Lifecycle::Running, std::memory_order_release);

    if (m_memory.hardLimit > 0) {
        m_state = m_hasSeed
            ? CreateLuaState(&LuaRuntime::memoryAllocator, &m_memory, m_seed)
            : CreateLuaState(&LuaRuntime::memoryAllocator, &m_memory);
    } else {
        m_state = m_hasSeed ? CreateLuaState(m_seed) : CreateLuaState();
    }
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
    globalCardLifetimeManager().registerRuntimeDomain(lifetimeDomain(), this,
                                                      m_generation, m_state);
    return true;
}

void LuaRuntime::setSeed(quint64 seed)
{
    Q_ASSERT(!m_state);
    m_seed = seed;
    m_hasSeed = true;
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
    LuaInvocationScope invocation(*this);
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
    if (!m_state || lifecycle() != Lifecycle::Running
        || !isCurrentThreadOwner() || currentRuntime != this)
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

int LuaRuntime::protectedCall(lua_State *state, int argumentCount,
                              int resultCount, int errorFunction)
{
    LuaRuntime *runtime = fromState(state);
    if (!runtime)
        return lua_pcall(state, argumentCount, resultCount, errorFunction);

    if (currentRuntime == runtime) {
        LuaInvocationScope invocation(*runtime);
        return lua_pcall(state, argumentCount, resultCount, errorFunction);
    }

    Binding binding(*runtime, false);
    LuaInvocationScope invocation(*runtime);
    return lua_pcall(state, argumentCount, resultCount, errorFunction);
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
    m_previousContext = CardLifetimeManager::setCurrentRuntimeContext(
        runtime.lifetimeDomain(), &runtime, runtime.generation(), runtime.rawState());
    currentRuntime = &runtime;
}

LuaRuntime::Binding::~Binding()
{
    currentRuntime = m_previous;
    CardLifetimeManager::setCurrentRuntimeContext(m_previousContext.domain,
                                                  m_previousContext.identity,
                                                  m_previousContext.generation,
                                                  m_previousContext.state);
    m_runtime->m_executionMutex.unlock();
}

LuaRuntime::LuaInvocationScope::LuaInvocationScope(LuaRuntime &runtime)
    : m_runtime(runtime.beginInvocation() ? &runtime : nullptr)
{
}

LuaRuntime::LuaInvocationScope::~LuaInvocationScope()
{
    if (m_runtime)
        m_runtime->endInvocation();
}

bool LuaRuntime::beginInvocation()
{
    QMutexLocker lock(&m_executionMutex);
    if (m_lifecycle.load(std::memory_order_acquire) != Lifecycle::Running)
        return false;
    const int previousDepth = m_invocationDepth.fetch_add(1, std::memory_order_acq_rel);
    if (previousDepth == 0)
        m_luaPinManager = CardLifetimeManager::enterLuaPinForCurrentThread();
    return true;
}

void LuaRuntime::endInvocation()
{
    QMutexLocker lock(&m_executionMutex);
    int depth = m_invocationDepth.load(std::memory_order_acquire);
    if (depth > 0) {
        m_invocationDepth.fetch_sub(1, std::memory_order_acq_rel);
        if (depth == 1) {
            CardLifetimeManager::leaveLuaPinForCurrentThread(m_luaPinManager);
            m_luaPinManager = nullptr;
        }
    }
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

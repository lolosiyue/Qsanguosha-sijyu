#ifndef QSAN_LUA_RUNTIME_H
#define QSAN_LUA_RUNTIME_H

#include "card-lifetime-manager.h"

#include <QMutex>
#include <QString>
#include <QThread>

#include <atomic>
#include <cstddef>

struct lua_State;

class LuaRuntime
{
public:
    enum Kind { Bootstrap, Game, Client, Auxiliary };
    enum class Lifecycle : quint8 { Running, Closing, Closed };

    explicit LuaRuntime(Kind kind);
    ~LuaRuntime();

    LuaRuntime(const LuaRuntime &) = delete;
    LuaRuntime &operator=(const LuaRuntime &) = delete;

    bool initialize(QString *error = nullptr);
    void setSeed(quint64 seed);
    void setMemoryLimits(size_t softLimit, size_t hardLimit);
    bool addPackagePath(const QString &pattern, QString *error = nullptr);
    bool loadScript(const QString &path, QString *error = nullptr);
    void shutdown();

    void setLifetimeDomain(const void *domain) { m_lifetimeDomain = domain; }
    const void *lifetimeDomain() const { return m_lifetimeDomain ? m_lifetimeDomain : this; }

    lua_State *state() const;
    lua_State *rawState() const { return m_state; }
    quint64 generation() const { return m_generation; }
    Kind kind() const { return m_kind; }
    size_t memoryBytes() const { return m_memory.current; }
    size_t peakMemoryBytes() const { return m_memory.peak; }
    bool exceedsSoftMemoryLimit() const {
        return m_memory.softLimit > 0 && m_memory.current > m_memory.softLimit;
    }

    bool isCurrentThreadOwner() const;
    int invocationDepth() const { return m_invocationDepth.load(std::memory_order_acquire); }
    Lifecycle lifecycle() const { return m_lifecycle.load(std::memory_order_acquire); }
    bool isClosing() const { return lifecycle() == Lifecycle::Closing; }
    bool isClosed() const { return lifecycle() == Lifecycle::Closed; }

    static LuaRuntime *fromState(lua_State *state);
    static LuaRuntime *current();
    static lua_State *currentState();
    static void setCurrentForThread(LuaRuntime *runtime);

    class Binding
    {
    public:
        explicit Binding(LuaRuntime &runtime, bool adoptOwner = true);
        ~Binding();

        Binding(const Binding &) = delete;
        Binding &operator=(const Binding &) = delete;

    private:
        LuaRuntime *m_runtime;
        LuaRuntime *m_previous;
        CardLifetimeRuntimeContext m_previousContext;
    };

    class LuaInvocationScope
    {
    public:
        explicit LuaInvocationScope(LuaRuntime &runtime);
        ~LuaInvocationScope();
        LuaInvocationScope(const LuaInvocationScope &) = delete;
        LuaInvocationScope &operator=(const LuaInvocationScope &) = delete;
    private:
        LuaRuntime *m_runtime;
    };

private:
    struct MemoryState {
        size_t current = 0;
        size_t peak = 0;
        size_t softLimit = 0;
        size_t hardLimit = 0;
    };

    static QMutex &registryMutex();
    static void *memoryAllocator(void *userData, void *pointer,
                                 size_t oldSize, size_t newSize);

    void adoptCurrentThread();
    bool beginInvocation();
    void endInvocation();

    Kind m_kind;
    lua_State *m_state;
    quint64 m_generation;
    quint64 m_seed;
    bool m_hasSeed;
    QThread *m_owner;
    QRecursiveMutex m_executionMutex;
    std::atomic<int> m_invocationDepth{0};
    std::atomic<Lifecycle> m_lifecycle{Lifecycle::Running};
    MemoryState m_memory;
    const void *m_lifetimeDomain = nullptr;
};

class LuaCallbackRef
{
public:
    LuaCallbackRef();
    LuaCallbackRef(int nullValue);
    LuaCallbackRef(lua_State *state, int registryRef);

    LuaCallbackRef &operator=(int nullValue);

    bool isValid() const;
    explicit operator bool() const { return isValid(); }
    bool operator==(int nullValue) const;
    bool operator!=(int nullValue) const { return !(*this == nullValue); }

    bool push(lua_State *target) const;
    lua_State *state() const;
    int registryRef() const { return m_registryRef; }
    quint64 generation() const { return m_generation; }

private:
    lua_State *m_state;
    quint64 m_generation;
    int m_registryRef;
};

typedef LuaCallbackRef LuaFunction;

#endif

#ifndef QSAN_LUA_RUNTIME_H
#define QSAN_LUA_RUNTIME_H

#include "card-lifetime-manager.h"

#include <QHash>
#include <QMutex>
#include <QString>
#include <QThread>
#include <QVariant>

#include <atomic>
#include <cstddef>
#include <functional>

struct lua_State;
struct lua_Debug;

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
    // Native source-origin event, offered once before this runtime's FIRST
    // initialize. Reused states and later initializations cannot replay it.
    // This authenticates the fixed CreateLuaState -> native SGS construction
    // boundary only. A consumer must independently prove every subsequent
    // rules/helper/native/async effect before using any retained absence.
    class NativeNamespaceBirth final {
    public:
        NativeNamespaceBirth(const NativeNamespaceBirth &) = delete;
        NativeNamespaceBirth &operator=(const NativeNamespaceBirth &) = delete;
        bool isCurrent() const noexcept;
        lua_State *state() const noexcept { return m_state; }
        quint64 generation() const noexcept { return m_generation; }
        const void *globalsIdentity() const noexcept { return m_globals; }
        const void *namespaceIdentity() const noexcept { return m_space; }
    private:
        friend class LuaRuntime;
        NativeNamespaceBirth(LuaRuntime &, const void *, const void *) noexcept;
        LuaRuntime *m_runtime;
        lua_State *m_state;
        quint64 m_generation;
        const void *m_globals;
        const void *m_space;
    };
    bool installNativeNamespaceBirthObserver(const void *owner,
        std::function<void(const NativeNamespaceBirth &)> observer);
    void setSeed(quint64 seed);
    void setMemoryLimits(size_t softLimit, size_t hardLimit);
    bool addPackagePath(const QString &pattern, QString *error = nullptr);
    bool loadScript(const QString &path, QString *error = nullptr);
    // Takeover state is an explicit, versioned provider contract.  The Lua VM
    // itself is deliberately never serialized.
    bool exportTakeoverState(QVariantMap &state, QString *error = nullptr);
    bool restoreTakeoverState(const QVariantMap &state, QString *error = nullptr);
    void shutdown();

    void setLifetimeDomain(const void *domain) { m_lifetimeDomain = domain; }
    const void *lifetimeDomain() const { return m_lifetimeDomain ? m_lifetimeDomain : this; }

    lua_State *state() const;
    lua_State *rawState() const { return m_state; }
    quint64 generation() const { return m_generation; }
    // Observation-only epoch: a swallowed author error is not a completed effect.
    quint64 protectedCallFailureGeneration() const { return m_protectedCallFailures; }
    int protectedCallDepth() const { return m_protectedCallDepth; }
    quint64 protectedCallFailureGenerationAtDepth(int depth) const { return m_protectedCallFailuresByDepth.value(depth); }
    // Native VM bridge only. Finalizers/debug hooks have no certified
    // current request; the sole observer receives negative argumentCount.
    // Observer callbacks are a noexcept metadata/invalidator contract.
    void notifyUnclassifiedLuaEntry(lua_State *state, int asyncKind) noexcept;
    bool hasProtectedEntryObserver() const noexcept { return bool(m_protectedEntryObserver); }
    // Native provenance only: called after successful parsing, before a loaded
    // chunk can execute. It never modifies the chunk, return value or bindings.
    // The observer must not execute Lua, grow the Lua stack, or throw.
    bool installLoadedChunkObserver(const void *owner,
        std::function<void(lua_State *, int)> observer);
    void removeLoadedChunkObserver(const void *owner) noexcept;
    void notifyLoadedChunk(lua_State *state, int kind = 5) noexcept;
    // Bootstrap's independent monotonic witness is installed before SmartAI
    // creates any registry. The later ordinary observer keeps its sole owner.
    // Receives VM kinds 3/4/7, or -(argumentCount+1) before native protectedCall.
    // Negative entries identify the actual function at -(argumentCount+1);
    // observers are noexcept metadata-only and must not mutate/grow the stack.
    // Invalid argument counts use unknown sentinel 0 (no signed overflow).
    // Covers this native transport only, not Lua pcall/metamethods/lua_call;
    // those descendants require the complete fixed synchronous source proof.
    bool installBootstrapAsyncObserver(const void *owner,
        std::function<void(lua_State *, int)> observer);
    void removeBootstrapAsyncObserver(const void *owner) noexcept;
    // Native-only lexical proof for the compiled AI instruction-budget hook.
    // The caller must use the fixed budget-only implementation, never a Lua
    // callback trampoline. Unknown/replaced hooks continue to invalidate.
    class InstructionBudgetScope {
    public:
        InstructionBudgetScope(lua_State *, void (*)(lua_State *,lua_Debug *)) noexcept;
        ~InstructionBudgetScope();
        InstructionBudgetScope(const InstructionBudgetScope &)=delete;
        InstructionBudgetScope &operator=(const InstructionBudgetScope &)=delete;
        static bool matches(lua_State *) noexcept;
    private:
        lua_State *m_state; void (*m_hook)(lua_State *,lua_Debug *);
        InstructionBudgetScope *m_previous;
        static thread_local InstructionBudgetScope *s_current;
    };
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
    // One native owner; invoked before every protected entry, including coroutine
    // states bound to this runtime. No Lua-visible setter or policy authority.
    bool installProtectedEntryObserver(const void *owner,
        std::function<void(lua_State *, int)> observer);

    static int protectedCall(lua_State *state, int argumentCount,
                             int resultCount, int errorFunction);
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
    static int luaRegisterTakeoverStateProvider(lua_State *state);

    struct TakeoverStateProvider {
        int version = 0;
        int exportRef = -2; // LUA_NOREF; keep the public header Lua-independent.
        int restoreRef = -2;
    };

    bool registerTakeoverStateProvider(const QString &name, int version,
                                       int exportRef, int restoreRef,
                                       QString *error = nullptr);
    void clearTakeoverStateProviders();

    void adoptCurrentThread();
    bool beginInvocation();
    void endInvocation();

    void notifyNativeNamespaceBirth() noexcept;
    bool m_nativeBirthOffered = false;
    const void *m_nativeBirthOwner = nullptr;
    std::function<void(const NativeNamespaceBirth &)> m_nativeBirthObserver;
    const NativeNamespaceBirth *m_activeNativeBirth = nullptr;
    Kind m_kind;
    lua_State *m_state;
    quint64 m_generation;
    const void *m_protectedEntryOwner = nullptr;
    std::function<void(lua_State *, int)> m_protectedEntryObserver;
    const void *m_loadedChunkOwner = nullptr;
    std::function<void(lua_State *, int)> m_loadedChunkObserver;
    const void *m_bootstrapAsyncOwner = nullptr;
    std::function<void(lua_State *, int)> m_bootstrapAsyncObserver;
    quint64 m_protectedCallFailures = 0;
    int m_protectedCallDepth = 0;
    QHash<int, quint64> m_protectedCallFailuresByDepth;
    quint64 m_seed;
    bool m_hasSeed;
    QThread *m_owner;
    QRecursiveMutex m_executionMutex;
    std::atomic<int> m_invocationDepth{0};
    CardLifetimeManager *m_luaPinManager = nullptr;
    std::atomic<Lifecycle> m_lifecycle{Lifecycle::Running};
    MemoryState m_memory;
    const void *m_lifetimeDomain = nullptr;
    QHash<QString, TakeoverStateProvider> m_takeoverStateProviders;
};

class LuaCallbackRef
{
public:
    LuaCallbackRef();
    LuaCallbackRef(int nullValue);
    LuaCallbackRef(const LuaCallbackRef &other);
    LuaCallbackRef &operator=(const LuaCallbackRef &other);
    LuaCallbackRef(lua_State *state, int registryRef);

    LuaCallbackRef &operator=(int nullValue);

    bool isValid() const;
    explicit operator bool() const { return isValid(); }
    bool operator==(int nullValue) const;
    bool operator!=(int nullValue) const { return !(*this == nullValue); }

    bool push(lua_State *target) const;
    lua_State *state() const;
    int registryRef() const;
    quint64 generation() const;
    // Birth-only absence fact. Every assignment, including equal/null/self and
    // copied handles, retires it. It never inspects a current reference value.
    bool hasOriginalNullSource() const;

private:
    // Protect this scalar tuple only. No lock across Lua/registry access,
    // callback execution, QObject operations or logging.
    mutable QMutex m_tupleMutex;
    bool m_originalNull = true;
    lua_State *m_state;
    quint64 m_generation;
    int m_registryRef;
};

typedef LuaCallbackRef LuaFunction;

#endif

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
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <utility>

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

void drainLifetimeDomain(CardLifetimeManager &manager, const void *domain)
{
    QList<QPointer<QObject>> retiredObjects;
    manager.drainDomain(domain, &retiredObjects);
    if (!QCoreApplication::instance())
        return;

    // Do not dispatch another Room's queued destructor while this runtime closes.
    for (const QPointer<QObject> &object : std::as_const(retiredObjects))
        if (object)
            QCoreApplication::sendPostedEvents(
                object.data(), QEvent::DeferredDelete);
}

struct LuaTableGuard {
    QSet<const void *> &tables;
    const void *pointer;
    ~LuaTableGuard() { tables.remove(pointer); }
};

bool luaValueToVariant(lua_State *state, int index, QVariant &value,
                       QString &error, QSet<const void *> &tables, int depth)
{
    if (depth > 64) {
        error = QStringLiteral("takeover provider state exceeds table depth limit");
        return false;
    }

    const int type = lua_type(state, index);
    switch (type) {
    case LUA_TNIL:
        value = QVariant();
        return true;
    case LUA_TBOOLEAN:
        value = QVariant(lua_toboolean(state, index) != 0);
        return true;
    case LUA_TNUMBER:
        if (lua_isinteger(state, index)) {
            value = QVariant::fromValue(static_cast<qlonglong>(lua_tointeger(state, index)));
            return true;
        }
        if (!std::isfinite(lua_tonumber(state, index))) {
            error = QStringLiteral("takeover provider state contains a non-finite number");
            return false;
        }
        value = QVariant(lua_tonumber(state, index));
        return true;
    case LUA_TSTRING:
    {
        size_t length = 0;
        const char *bytes = lua_tolstring(state, index, &length);
        value = QString::fromUtf8(bytes, static_cast<qsizetype>(length));
        return true;
    }
    case LUA_TTABLE:
        break;
    default:
        error = QStringLiteral("takeover provider state contains unsupported Lua type: %1")
                    .arg(QString::fromLatin1(lua_typename(state, type)));
        return false;
    }

    const int absoluteIndex = lua_absindex(state, index);
    const void *pointer = lua_topointer(state, absoluteIndex);
    if (!pointer || tables.contains(pointer)) {
        error = QStringLiteral("takeover provider state contains a cyclic table");
        return false;
    }
    tables.insert(pointer);
    const LuaTableGuard guard{tables, pointer};

    bool hasIntegerKey = false;
    bool hasStringKey = false;
    qsizetype integerKeyCount = 0;
    lua_pushnil(state);
    while (lua_next(state, absoluteIndex) != 0) {
        if (lua_isinteger(state, -2)) {
            hasIntegerKey = true;
            ++integerKeyCount;
        } else if (lua_type(state, -2) == LUA_TSTRING) {
            hasStringKey = true;
        } else {
            lua_pop(state, 2);
            error = QStringLiteral("takeover provider state table keys must be strings or integers");
            return false;
        }
        lua_pop(state, 1);
    }
    if (hasIntegerKey && hasStringKey) {
        error = QStringLiteral("takeover provider state table mixes array and object keys");
        return false;
    }

    if (hasIntegerKey) {
        const size_t rawLength = lua_rawlen(state, absoluteIndex);
        if (integerKeyCount != static_cast<qsizetype>(rawLength)) {
            error = QStringLiteral("takeover provider state array has sparse or invalid indices");
            return false;
        }
        QVariantList list;
        list.reserve(static_cast<int>(rawLength));
        for (size_t i = 1; i <= rawLength; ++i) {
            lua_rawgeti(state, absoluteIndex, static_cast<lua_Integer>(i));
            if (lua_isnil(state, -1)) {
                lua_pop(state, 1);
                error = QStringLiteral("takeover provider state array contains a hole");
                return false;
            }
            QVariant child;
            if (!luaValueToVariant(state, -1, child, error, tables, depth + 1)) {
                lua_pop(state, 1);
                return false;
            }
            lua_pop(state, 1);
            list.append(child);
        }
        value = list;
        return true;
    }

    QVariantMap map;
    lua_pushnil(state);
    while (lua_next(state, absoluteIndex) != 0) {
        size_t keyLength = 0;
        const char *keyBytes = lua_tolstring(state, -2, &keyLength);
        const QString key = QString::fromUtf8(
            keyBytes, static_cast<qsizetype>(keyLength));
        QVariant child;
        if (!luaValueToVariant(state, -1, child, error, tables, depth + 1)) {
            lua_pop(state, 2);
            return false;
        }
        map.insert(key, child);
        lua_pop(state, 1);
    }
    value = map;
    return true;
}

bool isJsonSafeVariant(const QVariant &value, QString &error, int depth = 0)
{
    if (depth > 64) {
        error = QStringLiteral("takeover provider state exceeds variant depth limit");
        return false;
    }
    if (!value.isValid() || value.isNull())
        return true;

    switch (value.userType()) {
    case QMetaType::Bool:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::Char:
    case QMetaType::SChar:
    case QMetaType::UChar:
        return true;
    case QMetaType::LongLong: {
        constexpr qlonglong JsonExactIntegerLimit = 9007199254740991LL;
        const qlonglong number = value.toLongLong();
        if (number >= -JsonExactIntegerLimit && number <= JsonExactIntegerLimit)
            return true;
        error = QStringLiteral("takeover provider integer is not exactly representable in JSON");
        return false;
    }
    case QMetaType::ULongLong:
        if (value.toULongLong() <= 9007199254740991ULL)
            return true;
        error = QStringLiteral("takeover provider integer is not exactly representable in JSON");
        return false;
    case QMetaType::Double:
    case QMetaType::Float:
        if (!std::isfinite(value.toDouble())) {
            error = QStringLiteral("takeover provider state contains a non-finite number");
            return false;
        }
        return true;
    case QMetaType::QString:
        return true;
    case QMetaType::QStringList:
        return true;
    case QMetaType::QVariantList: {
        const QVariantList list = value.toList();
        for (const QVariant &child : list)
            if (!isJsonSafeVariant(child, error, depth + 1))
                return false;
        return true;
    }
    case QMetaType::QVariantMap: {
        const QVariantMap map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it)
            if (!isJsonSafeVariant(it.value(), error, depth + 1))
                return false;
        return true;
    }
    default:
        error = QStringLiteral("takeover provider state contains unsupported QVariant type: %1")
                    .arg(QString::fromLatin1(value.typeName()));
        return false;
    }
}

bool pushVariant(lua_State *state, const QVariant &value, QString &error, int depth = 0)
{
    if (depth > 64) {
        error = QStringLiteral("takeover provider state exceeds variant depth limit");
        return false;
    }
    if (!value.isValid() || value.isNull()) {
        lua_pushnil(state);
        return true;
    }

    switch (value.userType()) {
    case QMetaType::Bool:
        lua_pushboolean(state, value.toBool());
        return true;
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::Char:
    case QMetaType::SChar:
    case QMetaType::UChar:
        lua_pushinteger(state, static_cast<lua_Integer>(value.toLongLong()));
        return true;
    case QMetaType::Double:
    case QMetaType::Float:
        if (!std::isfinite(value.toDouble())) {
            error = QStringLiteral("takeover provider state contains a non-finite number");
            return false;
        }
        lua_pushnumber(state, value.toDouble());
        return true;
    case QMetaType::QString: {
        const QByteArray bytes = value.toString().toUtf8();
        lua_pushlstring(state, bytes.constData(), static_cast<size_t>(bytes.size()));
        return true;
    }
    case QMetaType::QStringList: {
        const QStringList list = value.toStringList();
        lua_createtable(state, list.size(), 0);
        for (int i = 0; i < list.size(); ++i) {
            const QByteArray bytes = list.at(i).toUtf8();
            lua_pushlstring(state, bytes.constData(), static_cast<size_t>(bytes.size()));
            lua_rawseti(state, -2, i + 1);
        }
        return true;
    }
    case QMetaType::QVariantList: {
        const QVariantList list = value.toList();
        lua_createtable(state, list.size(), 0);
        for (int i = 0; i < list.size(); ++i) {
            if (!pushVariant(state, list.at(i), error, depth + 1)) {
                lua_pop(state, 1);
                return false;
            }
            lua_rawseti(state, -2, i + 1);
        }
        return true;
    }
    case QMetaType::QVariantMap: {
        const QVariantMap map = value.toMap();
        lua_createtable(state, 0, map.size());
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            const QByteArray keyBytes = it.key().toUtf8();
            lua_pushlstring(state, keyBytes.constData(),
                            static_cast<size_t>(keyBytes.size()));
            if (!pushVariant(state, it.value(), error, depth + 1)) {
                lua_pop(state, 2);
                return false;
            }
            lua_settable(state, -3);
        }
        return true;
    }
    default:
        error = QStringLiteral("takeover provider state contains unsupported QVariant type: %1")
                    .arg(QString::fromLatin1(value.typeName()));
        return false;
    }
}

QString luaStackError(lua_State *state)
{
    size_t length = 0;
    const char *message = lua_tolstring(state, -1, &length);
    return message ? QString::fromUtf8(message, static_cast<qsizetype>(length))
                   : QStringLiteral("unknown Lua takeover provider error");
}

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
    const void *domain = lifetimeDomain();
    drainLifetimeDomain(manager, domain);
    {
        QMutexLocker locker(&registryMutex());
        runtimeRegistry().remove(m_state);
    }
    clearTakeoverStateProviders();
    lua_State *closingState = m_state;
    lua_close(closingState);
    m_state = nullptr;
    manager.releaseWrapperBindings(domain, this, m_generation, closingState);
    drainLifetimeDomain(manager, domain);
    manager.drainDomain(domain);
    manager.unregisterRuntimeDomain(domain, this, m_generation, closingState);
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
    // lua/sanguosha.lua exposes the public sgs.RegisterTakeoverStateProvider
    // wrapper around this runtime-local function.
    lua_pushcfunction(m_state, &LuaRuntime::luaRegisterTakeoverStateProvider);
    lua_setglobal(m_state, "__qsan_register_takeover_state_provider");
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

bool LuaRuntime::registerTakeoverStateProvider(const QString &name, int version,
                                               int exportRef, int restoreRef,
                                               QString *error)
{
    if (name.isEmpty() || version <= 0 || exportRef < 0 || restoreRef < 0) {
        if (error)
            *error = QStringLiteral("invalid takeover state provider registration");
        return false;
    }

    const auto existing = m_takeoverStateProviders.constFind(name);
    if (existing != m_takeoverStateProviders.cend()) {
        if (m_state && existing->exportRef >= 0)
            luaL_unref(m_state, LUA_REGISTRYINDEX, existing->exportRef);
        if (m_state && existing->restoreRef >= 0)
            luaL_unref(m_state, LUA_REGISTRYINDEX, existing->restoreRef);
    }
    m_takeoverStateProviders.insert(name, TakeoverStateProvider{version, exportRef, restoreRef});
    return true;
}

int LuaRuntime::luaRegisterTakeoverStateProvider(lua_State *state)
{
    LuaRuntime *runtime = LuaRuntime::fromState(state);
    if (!runtime || runtime->state() != state)
        return luaL_error(state, "takeover provider registration requires the current Lua runtime");

    const char *name = luaL_checkstring(state, 1);
    const lua_Integer version = luaL_checkinteger(state, 2);
    if (version <= 0
        || version > static_cast<lua_Integer>(std::numeric_limits<int>::max())) {
        return luaL_error(state, "takeover provider version is out of range");
    }
    luaL_checktype(state, 3, LUA_TFUNCTION);
    luaL_checktype(state, 4, LUA_TFUNCTION);

    lua_pushvalue(state, 3);
    const int exportRef = luaL_ref(state, LUA_REGISTRYINDEX);
    lua_pushvalue(state, 4);
    const int restoreRef = luaL_ref(state, LUA_REGISTRYINDEX);

    QString error;
    if (!runtime->registerTakeoverStateProvider(QString::fromUtf8(name),
                                                static_cast<int>(version),
                                                exportRef, restoreRef, &error)) {
        luaL_unref(state, LUA_REGISTRYINDEX, exportRef);
        luaL_unref(state, LUA_REGISTRYINDEX, restoreRef);
        return luaL_error(state, "%s", qPrintable(error));
    }
    lua_pushboolean(state, 1);
    return 1;
}

bool LuaRuntime::exportTakeoverState(QVariantMap &state, QString *error)
{
    state.clear();
    lua_State *lua = this->state();
    if (!lua) {
        if (error)
            *error = QStringLiteral("takeover export requires the current Lua runtime");
        return false;
    }

    QStringList names = m_takeoverStateProviders.keys();
    std::sort(names.begin(), names.end());
    for (const QString &name : names) {
        const TakeoverStateProvider provider = m_takeoverStateProviders.value(name);
        const int base = lua_gettop(lua);
        lua_rawgeti(lua, LUA_REGISTRYINDEX, provider.exportRef);
        if (!lua_isfunction(lua, -1)) {
            lua_settop(lua, base);
            if (error)
                *error = QStringLiteral("takeover provider '%1' export callback is unavailable").arg(name);
            return false;
        }
        LuaInvocationScope invocation(*this);
        const int result = lua_pcall(lua, 0, LUA_MULTRET, 0);
        if (result != LUA_OK) {
            const QString callbackError = luaStackError(lua);
            lua_settop(lua, base);
            if (error)
                *error = QStringLiteral("takeover provider '%1' export failed: %2")
                             .arg(name, callbackError);
            return false;
        }
        if (lua_gettop(lua) - base != 1) {
            lua_settop(lua, base);
            if (error)
                *error = QStringLiteral("takeover provider '%1' export must return exactly one value")
                             .arg(name);
            return false;
        }

        QVariant value;
        QSet<const void *> tables;
        QString conversionError;
        if (!luaValueToVariant(lua, -1, value, conversionError, tables, 0)
            || !isJsonSafeVariant(value, conversionError)) {
            lua_settop(lua, base);
            if (error)
                *error = QStringLiteral("takeover provider '%1' export is not JSON-safe: %2")
                             .arg(name, conversionError);
            return false;
        }
        lua_settop(lua, base);
        QVariantMap entry;
        entry.insert(QStringLiteral("version"), provider.version);
        entry.insert(QStringLiteral("state"), value);
        state.insert(name, entry);
    }
    return true;
}

bool LuaRuntime::restoreTakeoverState(const QVariantMap &state, QString *error)
{
    lua_State *lua = this->state();
    if (!lua) {
        if (error)
            *error = QStringLiteral("takeover restore requires the current Lua runtime");
        return false;
    }

    QString conversionError;
    if (!isJsonSafeVariant(state, conversionError)) {
        if (error)
            *error = QStringLiteral("takeover state is not JSON-safe: %1").arg(conversionError);
        return false;
    }

    QStringList expected = m_takeoverStateProviders.keys();
    QStringList actual = state.keys();
    std::sort(expected.begin(), expected.end());
    std::sort(actual.begin(), actual.end());
    if (expected != actual) {
        if (error)
            *error = QStringLiteral("takeover state provider set does not match the current runtime");
        return false;
    }

    for (const QString &name : expected) {
        const QVariantMap entry = state.value(name).toMap();
        const QVariant versionValue = entry.value(QStringLiteral("version"));
        const QVariant payload = entry.value(QStringLiteral("state"));
        const TakeoverStateProvider provider = m_takeoverStateProviders.value(name);
        if (!entry.contains(QStringLiteral("version"))
            || !entry.contains(QStringLiteral("state"))
            || versionValue.toInt() != provider.version
            || (versionValue.userType() != QMetaType::Int
                && versionValue.userType() != QMetaType::LongLong
                && versionValue.userType() != QMetaType::Double)) {
            if (error)
                *error = QStringLiteral("takeover provider '%1' is missing or has a version mismatch")
                             .arg(name);
            return false;
        }
        if (!isJsonSafeVariant(payload, conversionError)) {
            if (error)
                *error = QStringLiteral("takeover provider '%1' payload is not JSON-safe: %2")
                             .arg(name, conversionError);
            return false;
        }

        const int base = lua_gettop(lua);
        lua_rawgeti(lua, LUA_REGISTRYINDEX, provider.restoreRef);
        if (!lua_isfunction(lua, -1)) {
            lua_settop(lua, base);
            if (error)
                *error = QStringLiteral("takeover provider '%1' restore callback is unavailable").arg(name);
            return false;
        }
        if (!pushVariant(lua, payload, conversionError)) {
            lua_settop(lua, base);
            if (error)
                *error = QStringLiteral("takeover provider '%1' payload cannot be passed to Lua: %2")
                             .arg(name, conversionError);
            return false;
        }
        LuaInvocationScope invocation(*this);
        if (lua_pcall(lua, 1, 0, 0) != LUA_OK) {
            const QString callbackError = luaStackError(lua);
            lua_settop(lua, base);
            if (error)
                *error = QStringLiteral("takeover provider '%1' restore failed: %2")
                             .arg(name, callbackError);
            return false;
        }
        lua_settop(lua, base);
    }
    return true;
}

void LuaRuntime::clearTakeoverStateProviders()
{
    if (m_state) {
        for (const TakeoverStateProvider &provider : std::as_const(m_takeoverStateProviders)) {
            if (provider.exportRef >= 0)
                luaL_unref(m_state, LUA_REGISTRYINDEX, provider.exportRef);
            if (provider.restoreRef >= 0)
                luaL_unref(m_state, LUA_REGISTRYINDEX, provider.restoreRef);
        }
    }
    m_takeoverStateProviders.clear();
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

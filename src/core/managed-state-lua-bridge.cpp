#include "managed-state-lua-bridge.h"

#include "game-state-contract.h"
#include "lua-runtime.h"
#include "lua.hpp"

#include <QByteArray>
#include <QMetaType>
#include <QSet>

#include <cmath>
#include <cstring>
#include <limits>

namespace {

constexpr int StoreUpvalue = 1;
constexpr int RuntimeUpvalue = 2;
constexpr int GenerationLowUpvalue = 3;
constexpr int GenerationHighUpvalue = 4;
constexpr int ProviderUpvalue = 5;
constexpr int ArrayTagsUpvalue = 6;
constexpr int ArrayRuntimeUpvalue = 1;
constexpr int ArrayGenerationLowUpvalue = 2;
constexpr int ArrayGenerationHighUpvalue = 3;
constexpr int ArrayHelperTagsUpvalue = 4;
constexpr int MaximumDepth = 64;
constexpr qsizetype MaximumEntries = 100000;
constexpr qsizetype MaximumStringBytes = 16 * 1024 * 1024;
constexpr qsizetype MaximumIdentityBytes = 256;

enum class BridgeError {
    None,
    RuntimeUnavailable,
    WrongThread,
    InvalidArguments,
    UnsupportedValue,
    InvalidUtf8,
    InvalidOwnerKind,
    MissingProvider,
    MissingOwner,
    InvalidProviderState,
    InvalidStoredValue,
    UpdateRejected,
    Unexpected
};

const char *errorText(BridgeError error)
{
    switch (error) {
    case BridgeError::RuntimeUnavailable: return "managed-state runtime unavailable";
    case BridgeError::WrongThread: return "managed-state thread mismatch";
    case BridgeError::InvalidArguments: return "invalid managed-state arguments";
    case BridgeError::UnsupportedValue: return "unsupported managed-state value";
    case BridgeError::InvalidUtf8: return "managed-state strings must be valid UTF-8";
    case BridgeError::InvalidOwnerKind: return "owner kind must be player, card, or skill";
    case BridgeError::MissingProvider: return "managed-state provider unavailable";
    case BridgeError::MissingOwner: return "managed-state owner is unresolved";
    case BridgeError::InvalidProviderState: return "managed-state provider root is malformed";
    case BridgeError::InvalidStoredValue: return "managed-state contains an invalid stored value";
    case BridgeError::UpdateRejected: return "managed-state update rejected";
    case BridgeError::Unexpected: return "managed-state operation failed";
    case BridgeError::None: break;
    }
    return "managed-state operation failed";
}

int pushFailure(lua_State *state, BridgeError error)
{
    lua_pushnil(state);
    lua_pushstring(state, errorText(error));
    return 2;
}

bool isValidUtf8(const char *bytes, size_t length, qsizetype maximum,
                 QString *result)
{
    if (length > static_cast<size_t>(maximum)
        || length > static_cast<size_t>(std::numeric_limits<qsizetype>::max()))
        return false;
    const QByteArray encoded(bytes, static_cast<qsizetype>(length));
    const QString decoded = QString::fromUtf8(bytes, static_cast<qsizetype>(length));
    if (decoded.toUtf8() != encoded)
        return false;
    if (result)
        *result = decoded;
    return true;
}

bool readLuaString(lua_State *state, int index, qsizetype maximum,
                   QString &result, BridgeError &error)
{
    if (lua_type(state, index) != LUA_TSTRING) {
        error = BridgeError::InvalidArguments;
        return false;
    }
    size_t length = 0;
    const char *bytes = lua_tolstring(state, index, &length);
    if (!bytes || !isValidUtf8(bytes, length, maximum, &result)) {
        error = BridgeError::InvalidUtf8;
        return false;
    }
    return true;
}

bool pushQString(lua_State *state, const QString &value)
{
    const QByteArray bytes = value.toUtf8();
    if (QString::fromUtf8(bytes.constData(), bytes.size()) != value)
        return false;
    lua_pushlstring(state, bytes.constData(), static_cast<size_t>(bytes.size()));
    return true;
}

bool ownerKindAllowed(const QString &kind)
{
    return kind == QStringLiteral("player") || kind == QStringLiteral("card")
        || kind == QStringLiteral("skill");
}

bool ownerExists(const GameState::WorldState &world, const QString &kind,
                 const QString &id)
{
    if (kind == QStringLiteral("player")) return world.players.contains(id);
    if (kind == QStringLiteral("card")) return world.cards.contains(id);
    if (kind == QStringLiteral("skill")) return world.skills.contains(id);
    return false;
}

bool isVariantMap(const QVariant &value)
{
    return value.userType() == QMetaType::QVariantMap;
}

bool isValidQString(const QString &value, qsizetype maximum = MaximumStringBytes)
{
    const QByteArray bytes = value.toUtf8();
    return bytes.size() <= maximum
        && QString::fromUtf8(bytes.constData(), bytes.size()) == value;
}

bool validateUtf8Tree(const QVariant &value, int depth, BridgeError &error)
{
    if (depth > MaximumDepth) {
        error = BridgeError::InvalidStoredValue;
        return false;
    }
    if (value.userType() == QMetaType::QString) {
        if (!isValidQString(value.toString())) {
            error = BridgeError::InvalidUtf8;
            return false;
        }
    } else if (value.userType() == QMetaType::QVariantList) {
        for (const QVariant &child : value.toList())
            if (!validateUtf8Tree(child, depth + 1, error)) return false;
    } else if (value.userType() == QMetaType::QVariantMap) {
        const QVariantMap map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            if (!isValidQString(it.key())) {
                error = BridgeError::InvalidUtf8;
                return false;
            }
            if (!validateUtf8Tree(it.value(), depth + 1, error)) return false;
        }
    }
    return true;
}

bool validateManagedRoot(const QVariantMap &root, const GameState::WorldState &world,
                         QVariantMap &owners, BridgeError &error)
{
    if (root.size() != 1 || !root.contains(QStringLiteral("owners"))
        || !isVariantMap(root.value(QStringLiteral("owners")))) {
        error = BridgeError::InvalidProviderState;
        return false;
    }
    owners = root.value(QStringLiteral("owners")).toMap();
    for (auto kindIt = owners.cbegin(); kindIt != owners.cend(); ++kindIt) {
        const QString &kind = kindIt.key();
        if (!isValidQString(kind, MaximumIdentityBytes) || !ownerKindAllowed(kind)
            || !isVariantMap(kindIt.value())) {
            error = BridgeError::InvalidProviderState;
            return false;
        }
        const QVariantMap ownerMap = kindIt.value().toMap();
        for (auto ownerIt = ownerMap.cbegin(); ownerIt != ownerMap.cend(); ++ownerIt) {
            const QString &ownerId = ownerIt.key();
            if (ownerId.isEmpty() || !isValidQString(ownerId, MaximumIdentityBytes)
                || !ownerExists(world, kind, ownerId) || !isVariantMap(ownerIt.value())) {
                error = ownerExists(world, kind, ownerId)
                    ? BridgeError::InvalidProviderState : BridgeError::MissingOwner;
                return false;
            }
            const QVariantMap payload = ownerIt.value().toMap();
            for (auto valueIt = payload.cbegin(); valueIt != payload.cend(); ++valueIt) {
                if (valueIt.key().isEmpty() || !isValidQString(valueIt.key(), MaximumIdentityBytes)
                    || !valueIt.value().isValid()) {
                    error = BridgeError::InvalidStoredValue;
                    return false;
                }
                QString validationError;
                if (!GameState::validateValue(valueIt.value(), world, &validationError)) {
                    error = BridgeError::InvalidStoredValue;
                    return false;
                }
                if (!validateUtf8Tree(valueIt.value(), 0, error))
                    return false;
            }
        }
    }
    return true;
}

bool providerAvailable(const GameState::WorldState &world, const QString &providerId,
                       BridgeError &error)
{
    const auto provider = world.providers.constFind(providerId);
    if (provider == world.providers.cend() || provider->version <= 0) {
        error = BridgeError::MissingProvider;
        return false;
    }
    return true;
}

bool readCoordinates(lua_State *state, int expectedArguments, QString &kind,
                     QString &ownerId, QString &key, BridgeError &error)
{
    if (lua_gettop(state) != expectedArguments) {
        error = BridgeError::InvalidArguments;
        return false;
    }
    if (!readLuaString(state, 1, MaximumIdentityBytes, kind, error)
        || !readLuaString(state, 2, MaximumIdentityBytes, ownerId, error)
        || !readLuaString(state, 3, MaximumIdentityBytes, key, error))
        return false;
    if (!ownerKindAllowed(kind)) {
        error = BridgeError::InvalidOwnerKind;
        return false;
    }
    if (ownerId.isEmpty() || key.isEmpty()) {
        error = BridgeError::InvalidArguments;
        return false;
    }
    return true;
}

bool isTaggedArray(lua_State *state, int tableIndex)
{
    tableIndex = lua_absindex(state, tableIndex);
    lua_pushvalue(state, tableIndex);
    lua_rawget(state, lua_upvalueindex(ArrayTagsUpvalue));
    const bool tagged = lua_toboolean(state, -1) != 0;
    lua_pop(state, 1);
    return tagged;
}

bool luaValueToVariant(lua_State *state, int index, QVariant &value,
                       QSet<const void *> &seen, qsizetype &entryBudget,
                       int depth, BridgeError &error)
{
    if (depth > MaximumDepth) {
        error = BridgeError::UnsupportedValue;
        return false;
    }
    switch (lua_type(state, index)) {
    case LUA_TBOOLEAN:
        value = lua_toboolean(state, index) != 0;
        return true;
    case LUA_TNUMBER:
        if (lua_isinteger(state, index)) {
            value = QVariant::fromValue(static_cast<qlonglong>(lua_tointeger(state, index)));
            return true;
        }
        if (!std::isfinite(lua_tonumber(state, index))) {
            error = BridgeError::UnsupportedValue;
            return false;
        }
        value = QVariant(static_cast<double>(lua_tonumber(state, index)));
        return true;
    case LUA_TSTRING: {
        size_t length = 0;
        const char *bytes = lua_tolstring(state, index, &length);
        QString string;
        if (!bytes || !isValidUtf8(bytes, length, MaximumStringBytes, &string)) {
            error = BridgeError::InvalidUtf8;
            return false;
        }
        value = string;
        return true;
    }
    case LUA_TTABLE:
        break;
    default:
        error = BridgeError::UnsupportedValue;
        return false;
    }

    const int tableIndex = lua_absindex(state, index);
    if (lua_getmetatable(state, tableIndex)) {
        lua_pop(state, 1);
        error = BridgeError::UnsupportedValue;
        return false;
    }
    const void *identity = lua_topointer(state, tableIndex);
    if (!identity || seen.contains(identity)) {
        error = BridgeError::UnsupportedValue; // Both aliases and cycles are rejected.
        return false;
    }
    seen.insert(identity);
    const bool taggedArray = isTaggedArray(state, tableIndex);

    bool hasStringKey = false;
    bool hasIntegerKey = false;
    lua_Integer maximumIndex = 0;
    qsizetype keyCount = 0;
    lua_pushnil(state);
    while (lua_next(state, tableIndex) != 0) {
        ++keyCount;
        if (++entryBudget > MaximumEntries) {
            lua_pop(state, 2);
            error = BridgeError::UnsupportedValue;
            return false;
        }
        if (lua_type(state, -2) == LUA_TSTRING) {
            hasStringKey = true;
        } else if (lua_isinteger(state, -2)) {
            const lua_Integer arrayIndex = lua_tointeger(state, -2);
            if (arrayIndex < 1 || arrayIndex > MaximumEntries) {
                lua_pop(state, 2);
                error = BridgeError::UnsupportedValue;
                return false;
            }
            hasIntegerKey = true;
            maximumIndex = qMax(maximumIndex, arrayIndex);
        } else {
            lua_pop(state, 2);
            error = BridgeError::UnsupportedValue;
            return false;
        }
        lua_pop(state, 1);
    }
    if ((hasStringKey && hasIntegerKey) || (taggedArray && hasStringKey)) {
        error = BridgeError::UnsupportedValue;
        return false;
    }

    if (taggedArray || hasIntegerKey) {
        if (keyCount != maximumIndex) {
            error = BridgeError::UnsupportedValue;
            return false;
        }
        QVariantList list;
        list.reserve(static_cast<qsizetype>(maximumIndex));
        for (lua_Integer i = 1; i <= maximumIndex; ++i) {
            lua_rawgeti(state, tableIndex, i);
            if (lua_isnil(state, -1)) {
                lua_pop(state, 1);
                error = BridgeError::UnsupportedValue;
                return false;
            }
            QVariant child;
            if (!luaValueToVariant(state, -1, child, seen, entryBudget, depth + 1, error)) {
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
    while (lua_next(state, tableIndex) != 0) {
        QString key;
        if (!readLuaString(state, -2, MaximumStringBytes, key, error)) {
            lua_pop(state, 2);
            return false;
        }
        QVariant child;
        if (!luaValueToVariant(state, -1, child, seen, entryBudget, depth + 1, error)) {
            lua_pop(state, 2);
            return false;
        }
        map.insert(key, child);
        lua_pop(state, 1);
    }
    value = map;
    return true;
}

// This read-only result conversion uses allocating Lua APIs while QVariant
// values may be live. The current bridge relies on the engine's protected Lua
// invocation boundary for OOM reporting; it is deliberately never called by
// candidate preparation or native publication, where only C++ roots are swapped.
bool variantToLua(lua_State *state, const QVariant &value, int depth,
                  BridgeError &error)
{
    if (depth > MaximumDepth) {
        error = BridgeError::InvalidStoredValue;
        return false;
    }
    switch (value.userType()) {
    case QMetaType::Bool:
        lua_pushboolean(state, value.toBool());
        return true;
    case QMetaType::Int:
        lua_pushinteger(state, value.toInt());
        return true;
    case QMetaType::UInt:
        lua_pushinteger(state, static_cast<lua_Integer>(value.toUInt()));
        return true;
    case QMetaType::LongLong: {
        const qlonglong number = value.toLongLong();
        if (number < static_cast<qlonglong>(std::numeric_limits<lua_Integer>::min())
            || number > static_cast<qlonglong>(std::numeric_limits<lua_Integer>::max())) {
            error = BridgeError::InvalidStoredValue;
            return false;
        }
        lua_pushinteger(state, static_cast<lua_Integer>(number));
        return true;
    }
    case QMetaType::ULongLong: {
        const qulonglong number = value.toULongLong();
        if (number > static_cast<qulonglong>(std::numeric_limits<lua_Integer>::max())) {
            error = BridgeError::InvalidStoredValue;
            return false;
        }
        lua_pushinteger(state, static_cast<lua_Integer>(number));
        return true;
    }
    case QMetaType::Double: {
        const double number = value.toDouble();
        if (!std::isfinite(number)) {
            error = BridgeError::InvalidStoredValue;
            return false;
        }
        lua_pushnumber(state, number);
        return true;
    }
    case QMetaType::QString:
        if (!pushQString(state, value.toString())) {
            error = BridgeError::InvalidUtf8;
            return false;
        }
        return true;
    case QMetaType::QVariantList: {
        const QVariantList list = value.toList();
        lua_createtable(state, static_cast<int>(list.size()), 0);
        const int tableIndex = lua_absindex(state, -1);
        for (qsizetype i = 0; i < list.size(); ++i) {
            if (!variantToLua(state, list.at(i), depth + 1, error)) {
                lua_pop(state, 1);
                return false;
            }
            lua_rawseti(state, tableIndex, static_cast<lua_Integer>(i + 1));
        }
        if (list.isEmpty()) {
            lua_pushvalue(state, tableIndex);
            lua_pushboolean(state, 1);
            lua_rawset(state, lua_upvalueindex(ArrayTagsUpvalue));
        }
        return true;
    }
    case QMetaType::QVariantMap: {
        const QVariantMap map = value.toMap();
        lua_createtable(state, 0, static_cast<int>(map.size()));
        const int tableIndex = lua_absindex(state, -1);
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            if (!pushQString(state, it.key())) {
                lua_pop(state, 1);
                error = BridgeError::InvalidUtf8;
                return false;
            }
            if (!variantToLua(state, it.value(), depth + 1, error)) {
                lua_pop(state, 2);
                return false;
            }
            lua_rawset(state, tableIndex);
        }
        return true;
    }
    default:
        // In particular, reject invalid QVariant/nil and types such as QObject*.
        error = BridgeError::InvalidStoredValue;
        return false;
    }
}

bool validRuntimeContext(lua_State *state, LuaRuntime *expectedRuntime,
                         quint64 expectedGeneration, BridgeError &error)
{
    LuaRuntime *runtime = LuaRuntime::fromState(state);
    if (!runtime || runtime != expectedRuntime || runtime->generation() != expectedGeneration
        || runtime->rawState() != state) {
        error = BridgeError::RuntimeUnavailable;
        return false;
    }
    // LuaRuntime's explicit Binding handoff updates its owner. This admits the
    // same VM on its current owner while rejecting calls from any other thread.
    if (!runtime->isCurrentThreadOwner() || LuaRuntime::current() != runtime
        || runtime->state() != state) {
        error = BridgeError::WrongThread;
        return false;
    }
    return true;
}

bool validProviderContext(lua_State *state, GameState::WorldStore *store,
                          LuaRuntime *expectedRuntime, quint64 expectedGeneration,
                          QString &providerId, BridgeError &error)
{
    if (!store || !validRuntimeContext(state, expectedRuntime, expectedGeneration, error)) {
        if (!store)
            error = BridgeError::RuntimeUnavailable;
        return false;
    }
    if (!readLuaString(state, lua_upvalueindex(ProviderUpvalue), MaximumIdentityBytes,
                       providerId, error))
        return false;
    return true;
}

bool extractCoordinatesAndContext(lua_State *state, int argumentCount,
                                  QString &providerId, QString &kind,
                                  QString &ownerId, QString &key,
                                  GameState::WorldStore *&store,
                                  BridgeError &error)
{
    store = static_cast<GameState::WorldStore *>(
        lua_touserdata(state, lua_upvalueindex(StoreUpvalue)));
    auto *runtime = static_cast<LuaRuntime *>(
        lua_touserdata(state, lua_upvalueindex(RuntimeUpvalue)));
    const quint64 generation = quint64(quint32(lua_tointeger(state, lua_upvalueindex(GenerationLowUpvalue))))
        | (quint64(quint32(lua_tointeger(state, lua_upvalueindex(GenerationHighUpvalue)))) << 32);
    if (!validProviderContext(state, store, runtime, generation, providerId, error))
        return false;
    return readCoordinates(state, argumentCount, kind, ownerId, key, error);
}

bool getManagedValue(const GameState::WorldState &world, const QString &providerId,
                     const QString &kind, const QString &ownerId, const QString &key,
                     QVariant &value, bool &found, BridgeError &error)
{
    if (!providerAvailable(world, providerId, error))
        return false;
    if (!ownerExists(world, kind, ownerId)) {
        error = BridgeError::MissingOwner;
        return false;
    }
    QVariantMap owners;
    const auto provider = world.providers.constFind(providerId);
    if (!validateManagedRoot(provider->state, world, owners, error))
        return false;
    const QVariant ownerMapValue = owners.value(kind);
    if (!ownerMapValue.isValid()) {
        found = false;
        return true;
    }
    if (!isVariantMap(ownerMapValue)) {
        error = BridgeError::InvalidProviderState;
        return false;
    }
    const QVariantMap ownerMap = ownerMapValue.toMap();
    const QVariant payloadValue = ownerMap.value(ownerId);
    if (!payloadValue.isValid()) {
        found = false;
        return true;
    }
    if (!isVariantMap(payloadValue)) {
        error = BridgeError::InvalidProviderState;
        return false;
    }
    const QVariantMap payload = payloadValue.toMap();
    found = payload.contains(key);
    if (!found)
        return true;
    value = payload.value(key);
    if (!value.isValid() || !GameState::validateValue(value, world, nullptr)) {
        error = BridgeError::InvalidStoredValue;
        return false;
    }
    return true;
}

bool mutateManagedValue(GameState::WorldStore &store, const QString &providerId,
                        const QString &kind, const QString &ownerId,
                        const QString &key, const QVariant *newValue,
                        bool &removed, BridgeError &error)
{
    const GameState::WorldState &current = store.state();
    QVariant ignored;
    bool found = false;
    if (!getManagedValue(current, providerId, kind, ownerId, key, ignored, found, error))
        return false;
    if (!newValue && !found) {
        removed = false;
        return true;
    }

    QString updateError;
    const bool updated = store.update([&](GameState::WorldState &world, QString *) {
        if (!providerAvailable(world, providerId, error)
            || !ownerExists(world, kind, ownerId)) {
            if (error == BridgeError::None)
                error = BridgeError::MissingOwner;
            return false;
        }
        auto provider = world.providers.find(providerId);
        QVariantMap owners;
        if (!validateManagedRoot(provider->state, world, owners, error))
            return false;

        QVariantMap ownerMap = owners.value(kind).toMap();
        QVariantMap payload = ownerMap.value(ownerId).toMap();
        if (newValue) {
            payload.insert(key, *newValue);
            ownerMap.insert(ownerId, payload);
            owners.insert(kind, ownerMap);
        } else {
            payload.remove(key);
            if (payload.isEmpty())
                ownerMap.remove(ownerId);
            else
                ownerMap.insert(ownerId, payload);
            if (ownerMap.isEmpty())
                owners.remove(kind);
            else
                owners.insert(kind, ownerMap);
        }
        provider->state = QVariantMap{{QStringLiteral("owners"), owners}};
        return true;
    }, &updateError);
    if (!updated) {
        if (error == BridgeError::None)
            error = BridgeError::UpdateRejected;
        return false;
    }
    removed = !newValue && found;
    return true;
}

int managedGet(lua_State *state)
{
    QString providerId, kind, ownerId, key;
    GameState::WorldStore *store = nullptr;
    BridgeError error = BridgeError::None;
    if (!extractCoordinatesAndContext(state, 3, providerId, kind, ownerId, key,
                                      store, error))
        return pushFailure(state, error);
    const int base = lua_gettop(state);
    QVariant value;
    bool found = false;
    try {
        if (!getManagedValue(store->state(), providerId, kind, ownerId, key,
                             value, found, error)) {
            lua_settop(state, base);
            return pushFailure(state, error);
        }
    } catch (...) {
        lua_settop(state, base);
        return pushFailure(state, BridgeError::Unexpected);
    }
    if (!found) {
        lua_pushnil(state);
        return 1;
    }
    if (!variantToLua(state, value, 0, error)) {
        lua_settop(state, base);
        return pushFailure(state, error);
    }
    return 1;
}

int managedSet(lua_State *state)
{
    QString providerId, kind, ownerId, key;
    GameState::WorldStore *store = nullptr;
    BridgeError error = BridgeError::None;
    if (!extractCoordinatesAndContext(state, 4, providerId, kind, ownerId, key,
                                      store, error))
        return pushFailure(state, error);
    QVariant value;
    QSet<const void *> seen;
    qsizetype entryBudget = 0;
    try {
        if (!luaValueToVariant(state, 4, value, seen, entryBudget, 0, error))
            return pushFailure(state, error);
        bool removed = false;
        if (!mutateManagedValue(*store, providerId, kind, ownerId, key,
                                &value, removed, error))
            return pushFailure(state, error);
    } catch (...) {
        return pushFailure(state, BridgeError::Unexpected);
    }
    lua_pushboolean(state, 1);
    return 1;
}

int managedRemove(lua_State *state)
{
    QString providerId, kind, ownerId, key;
    GameState::WorldStore *store = nullptr;
    BridgeError error = BridgeError::None;
    if (!extractCoordinatesAndContext(state, 3, providerId, kind, ownerId, key,
                                      store, error))
        return pushFailure(state, error);
    try {
        bool removed = false;
        if (!mutateManagedValue(*store, providerId, kind, ownerId, key,
                                nullptr, removed, error))
            return pushFailure(state, error);
        lua_pushboolean(state, removed);
        return 1;
    } catch (...) {
        return pushFailure(state, BridgeError::Unexpected);
    }
}

int managedArray(lua_State *state)
{
    if (lua_gettop(state) != 0)
        return pushFailure(state, BridgeError::InvalidArguments);
    auto *runtime = static_cast<LuaRuntime *>(
        lua_touserdata(state, lua_upvalueindex(ArrayRuntimeUpvalue)));
    const quint64 generation = quint64(quint32(lua_tointeger(
        state, lua_upvalueindex(ArrayGenerationLowUpvalue))))
        | (quint64(quint32(lua_tointeger(
               state, lua_upvalueindex(ArrayGenerationHighUpvalue)))) << 32);
    BridgeError error = BridgeError::None;
    if (!validRuntimeContext(state, runtime, generation, error))
        return pushFailure(state, error);
    lua_createtable(state, 0, 0);
    lua_pushvalue(state, -1);
    lua_pushboolean(state, 1);
    lua_rawset(state, lua_upvalueindex(ArrayHelperTagsUpvalue));
    return 1;
}

void pushManagedClosure(lua_State *state, lua_CFunction callback,
                        const char *providerBytes, size_t providerLength,
                        int arrayTagsIndex)
{
    lua_pushvalue(state, 1); // WorldStore
    lua_pushvalue(state, 2); // LuaRuntime
    lua_pushvalue(state, 3); // generation low word
    lua_pushvalue(state, 4); // generation high word
    lua_pushlstring(state, providerBytes, providerLength);
    lua_pushvalue(state, arrayTagsIndex);
    lua_pushcclosure(state, callback, 6);
}

// Runs under LuaRuntime::protectedCall. Keep this C entry point free of C++
// objects with destructors: Lua allocation failures longjmp to the protected
// call boundary. Publication is the final raw table write after all closures
// and tables have been built.
int installManagedApi(lua_State *state)
{
    lua_rawgeti(state, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
    const int globalsIndex = lua_absindex(state, -1);
    lua_pushliteral(state, "sgs");
    lua_rawget(state, globalsIndex);
    if (!lua_istable(state, -1)) {
        lua_pushboolean(state, 0);
        lua_pushliteral(state, "managed bridge install requires an sgs table");
        return 2;
    }
    const int sgsIndex = lua_absindex(state, -1);
    lua_pushliteral(state, "ManagedState");
    lua_rawget(state, sgsIndex);
    if (!lua_isnil(state, -1)) {
        lua_pushboolean(state, 0);
        lua_pushliteral(state, "sgs.ManagedState is already defined");
        return 2;
    }
    lua_pop(state, 1);

    // Tags preserve the distinction between an empty object and an empty
    // array without putting sentinel keys into user-visible state tables.
    lua_newtable(state);
    const int arrayTagsIndex = lua_absindex(state, -1);
    lua_newtable(state);
    lua_pushliteral(state, "__mode");
    lua_pushliteral(state, "k");
    lua_rawset(state, -3);
    lua_setmetatable(state, arrayTagsIndex);

    lua_createtable(state, 0, 4);
    const int apiIndex = lua_absindex(state, -1);
    const char *providerBytes = static_cast<const char *>(lua_touserdata(state, 5));
    const size_t providerLength = static_cast<size_t>(lua_tointeger(state, 6));

    // The provider string is copied into each Lua closure while this
    // installation call is protected.
    pushManagedClosure(state, managedGet, providerBytes, providerLength, arrayTagsIndex);
    lua_setfield(state, apiIndex, "get");
    pushManagedClosure(state, managedSet, providerBytes, providerLength, arrayTagsIndex);
    lua_setfield(state, apiIndex, "set");
    pushManagedClosure(state, managedRemove, providerBytes, providerLength, arrayTagsIndex);
    lua_setfield(state, apiIndex, "remove");
    lua_pushvalue(state, 2); // LuaRuntime
    lua_pushvalue(state, 3); // generation low word
    lua_pushvalue(state, 4); // generation high word
    lua_pushvalue(state, arrayTagsIndex);
    lua_pushcclosure(state, managedArray, 4);
    lua_setfield(state, apiIndex, "array");

    lua_pushliteral(state, "ManagedState");
    lua_pushvalue(state, apiIndex);
    lua_rawset(state, sgsIndex);
    lua_pushboolean(state, 1);
    return 1;
}

bool validateProviderRoot(const GameState::WorldStore &store,
                          const QString &providerId, QString *error)
{
    const auto initialProvider = store.state().providers.constFind(providerId);
    if (initialProvider == store.state().providers.cend() || initialProvider->version <= 0) {
        if (error) *error = QStringLiteral("managed provider is not registered in the WorldStore");
        return false;
    }
    QVariantMap owners;
    BridgeError bridgeError = BridgeError::None;
    if (!validateManagedRoot(initialProvider->state, store.state(), owners, bridgeError)) {
        if (error) *error = QString::fromLatin1(errorText(bridgeError));
        return false;
    }
    return true;
}

} // namespace

bool ManagedStateLuaBridge::install(LuaRuntime &runtime,
                                    GameState::WorldStore &store,
                                    const QString &providerId,
                                    QString *error)
{
    if (error)
        error->clear();
    if (providerId.isEmpty() || providerId.toUtf8().size() > MaximumIdentityBytes) {
        if (error) *error = QStringLiteral("managed provider ID must be a bounded stable string");
        return false;
    }
    const QByteArray providerBytes = providerId.toUtf8();
    if (QString::fromUtf8(providerBytes.constData(), providerBytes.size()) != providerId) {
        if (error) *error = QStringLiteral("managed provider ID must be valid UTF-8");
        return false;
    }
    if (!runtime.isCurrentThreadOwner()) {
        if (error) *error = QStringLiteral("managed bridge install requires the Lua runtime owner thread");
        return false;
    }
    if (runtime.invocationDepth() != 0) {
        if (error) *error = QStringLiteral("managed bridge install requires a quiescent Lua runtime");
        return false;
    }
    if (!validateProviderRoot(store, providerId, error))
        return false;

    LuaRuntime::Binding binding(runtime, false);
    LuaRuntime::LuaInvocationScope invocation(runtime);
    lua_State *state = runtime.state();
    if (!state) {
        if (error) *error = QStringLiteral("managed bridge install requires current Lua runtime binding");
        return false;
    }
    const int base = lua_gettop(state);
    lua_pushcfunction(state, installManagedApi);
    lua_pushlightuserdata(state, &store);
    lua_pushlightuserdata(state, &runtime);
    const quint64 generation = runtime.generation();
    lua_pushinteger(state, static_cast<lua_Integer>(quint32(generation)));
    lua_pushinteger(state, static_cast<lua_Integer>(quint32(generation >> 32)));
    lua_pushlightuserdata(state, const_cast<char *>(providerBytes.constData()));
    lua_pushinteger(state, static_cast<lua_Integer>(providerBytes.size()));
    const int result = LuaRuntime::protectedCall(state, 6, 2, 0);
    if (result != LUA_OK) {
        const char *message = lua_tostring(state, -1);
        const QString luaError = QString::fromUtf8(message ? message : "Lua allocation failed");
        lua_settop(state, base);
        if (error) *error = QStringLiteral("managed bridge install failed: %1").arg(luaError);
        return false;
    }
    const bool installed = lua_toboolean(state, -2) != 0;
    if (!installed && error) {
        const char *message = lua_tostring(state, -1);
        *error = QString::fromUtf8(message ? message : "managed bridge install failed");
    }
    lua_settop(state, base);
    return installed;
}

%{

#include "resolution-history.h"

#include <QByteArray>
#include <QSet>
#include <QVariant>
#include <cmath>
#include <limits>

namespace {

constexpr int kHistoryLuaMaxDepth = 8;

bool readHistoryId(lua_State *state, int index, qint64 &id)
{
    if (lua_isinteger(state, index)) {
        id = static_cast<qint64>(lua_tointeger(state, index));
        return true;
    }
    if (lua_type(state, index) != LUA_TSTRING)
        return false;
    size_t length = 0;
    const char *raw = lua_tolstring(state, index, &length);
    if (!raw || length == 0 || length > 32)
        return false;
    bool ok = false;
    const qint64 parsed = QString::fromUtf8(raw, qsizetype(length)).toLongLong(&ok);
    if (!ok)
        return false;
    id = parsed;
    return true;
}

bool isHistoryFilterKey(const QByteArray &key)
{
    static const QSet<QByteArray> keys = {
        "kind", "event_id", "round_id", "turn_id", "phase_id", "player", "from", "to",
        "skill_name", "skill_owner", "after", "limit", "watermark"
    };
    return keys.contains(key);
}

bool readHistoryFilter(lua_State *state, int index, QVariantMap &filter)
{
    if (!lua_istable(state, index))
        return false;
    if (lua_rawlen(state, index) != 0)
        return false;

    const int table = lua_absindex(state, index);
    lua_pushnil(state);
    while (lua_next(state, table) != 0) {
        if (lua_type(state, -2) != LUA_TSTRING) {
            lua_pop(state, 2);
            return false;
        }
        size_t keyLength = 0;
        const char *keyRaw = lua_tolstring(state, -2, &keyLength);
        const QByteArray key(keyRaw, int(keyLength));
        if (!isHistoryFilterKey(key)) {
            lua_pop(state, 2);
            return false;
        }

        const int valueType = lua_type(state, -1);
        if (valueType == LUA_TBOOLEAN) {
            filter.insert(QString::fromUtf8(key), QVariant(lua_toboolean(state, -1) != 0));
        } else if (valueType == LUA_TSTRING) {
            size_t valueLength = 0;
            const char *valueRaw = lua_tolstring(state, -1, &valueLength);
            if (!valueRaw || valueLength > 64u * 1024u) {
                lua_pop(state, 2);
                return false;
            }
            filter.insert(QString::fromUtf8(key), QVariant(QString::fromUtf8(valueRaw, qsizetype(valueLength))));
        } else if (valueType == LUA_TNUMBER && lua_isinteger(state, -1)) {
            filter.insert(QString::fromUtf8(key), QVariant::fromValue<qint64>(
                static_cast<qint64>(lua_tointeger(state, -1))));
        } else if (valueType == LUA_TNUMBER) {
            const lua_Number value = lua_tonumber(state, -1);
            if (!std::isfinite(double(value))) {
                lua_pop(state, 2);
                return false;
            }
            filter.insert(QString::fromUtf8(key), QVariant(double(value)));
        } else {
            // Filters are deliberately scalar: tables, functions and userdata never cross this boundary.
            lua_pop(state, 2);
            return false;
        }
        lua_pop(state, 1);
    }
    return true;
}

bool validateHistoryVariant(const QVariant &value, int depth)
{
    if (depth > kHistoryLuaMaxDepth)
        return false;
    if (!value.isValid() || value.isNull())
        return true;
    if (value.userType() == QMetaType::Bool) {
        return true;
    } else if (value.canConvert<qint64>() && (value.userType() == QMetaType::LongLong
                                               || value.userType() == QMetaType::ULongLong
                                               || value.userType() == QMetaType::Int
                                               || value.userType() == QMetaType::UInt)) {
        return true;
    } else if (value.userType() == QMetaType::Double || value.userType() == QMetaType::Float) {
        return std::isfinite(value.toDouble());
    } else if (value.userType() == QMetaType::QString || value.userType() == QMetaType::QByteArray) {
        return true;
    } else if (value.userType() == QMetaType::QVariantList) {
        const QVariantList list = value.toList();
        for (const QVariant &item : list)
            if (!validateHistoryVariant(item, depth + 1))
                return false;
        return true;
    } else if (value.userType() == QMetaType::QVariantMap) {
        const QVariantMap map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it)
            if (!validateHistoryVariant(it.value(), depth + 1))
                return false;
        return true;
    }
    return false;
}

void pushHistoryVariant(lua_State *state, const QVariant &value, int depth)
{
    if (!value.isValid() || value.isNull()) {
        lua_pushnil(state);
    } else if (value.userType() == QMetaType::Bool) {
        lua_pushboolean(state, value.toBool());
    } else if (value.canConvert<qint64>() && (value.userType() == QMetaType::LongLong
                                               || value.userType() == QMetaType::ULongLong
                                               || value.userType() == QMetaType::Int
                                               || value.userType() == QMetaType::UInt)) {
        lua_pushinteger(state, static_cast<lua_Integer>(value.toLongLong()));
    } else if (value.userType() == QMetaType::Double || value.userType() == QMetaType::Float) {
        lua_pushnumber(state, value.toDouble());
    } else if (value.userType() == QMetaType::QString || value.userType() == QMetaType::QByteArray) {
        const QByteArray utf8 = value.toString().toUtf8();
        lua_pushlstring(state, utf8.constData(), size_t(utf8.size()));
    } else if (value.userType() == QMetaType::QVariantList) {
        const QVariantList list = value.toList();
        lua_createtable(state, 0, 0);
        for (qsizetype i = 0; i < list.size(); ++i) {
            pushHistoryVariant(state, list.at(i), depth + 1);
            lua_rawseti(state, -2, lua_Integer(i + 1));
        }
    } else if (value.userType() == QMetaType::QVariantMap) {
        const QVariantMap map = value.toMap();
        lua_createtable(state, 0, 0);
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            const QByteArray key = it.key().toUtf8();
            lua_pushlstring(state, key.constData(), size_t(key.size()));
            pushHistoryVariant(state, it.value(), depth + 1);
            lua_rawset(state, -3);
        }
    } else {
        lua_pushnil(state);
    }
}

} // namespace

%}

%{
using ResolutionHistoryLuaMap = QVariantMap;
%}

typedef QVariantMap ResolutionHistoryLuaMap;

%typemap(in) qint64 historyId {
    if (!readHistoryId(L, $input, $1)) {
        SWIG_Lua_pusherrstring(L, "expected a 64-bit history id as an integer or decimal string");
        SWIG_fail;
    }
}

%typemap(typecheck) qint64 historyId {
    $1 = lua_isinteger(L, $input) || lua_type(L, $input) == LUA_TSTRING;
}

%typemap(in) const QVariantMap &historyFilter (QVariantMap parsed) {
    if (!readHistoryFilter(L, $input, parsed)) {
        SWIG_Lua_pusherrstring(L, "expected a flat scalar history filter with known keys");
        SWIG_fail;
    }
    $1 = &parsed;
}

%typemap(out) ResolutionHistoryLuaMap {
    if (!validateHistoryVariant(QVariant($1), 0)) {
        SWIG_Lua_pusherrstring(L, "resolution history result contains unsupported or excessively nested values");
        SWIG_fail;
    }
    pushHistoryVariant(L, QVariant($1), 0);
    SWIG_arg++;
}

%extend Room {
    QString currentHistoryEventId() const {
        return QString::number($self->currentHistoryEventId());
    }

    ResolutionHistoryLuaMap historyEvent(qint64 historyId) const {
        return $self->historyEvent(historyId);
    }

    ResolutionHistoryLuaMap historyParent(qint64 historyId, const QString &kind,
                                          bool includeSelf = false) const {
        return $self->historyParent(historyId, kind, includeSelf);
    }

    ResolutionHistoryLuaMap historyScopes() const {
        return $self->historyScopes();
    }

    ResolutionHistoryLuaMap queryHistoryEvents(const QVariantMap &historyFilter) const {
        return $self->queryHistoryEvents(historyFilter);
    }

    ResolutionHistoryLuaMap queryHistoryMoves(const QVariantMap &historyFilter) const {
        return $self->queryHistoryMoves(historyFilter);
    }

    ResolutionHistoryLuaMap queryActualDamage(const QVariantMap &historyFilter) const {
        return $self->queryActualDamage(historyFilter);
    }

    ResolutionHistoryLuaMap queryHistoryFacts(const QVariantMap &historyFilter) const {
        return $self->queryHistoryFacts(historyFilter);
    }
};

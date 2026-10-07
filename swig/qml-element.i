%{

#include <QVariant>
#include <cmath>

namespace {

constexpr int kQmlDataMaxDepth = 8;

bool readQmlDataTable(lua_State *L, int index, QVariant &out, int depth);

bool readQmlDataValue(lua_State *L, int index, QVariant &out, int depth)
{
    switch (lua_type(L, index)) {
    case LUA_TNIL:
        out = QVariant();
        return true;
    case LUA_TBOOLEAN:
        out = QVariant(lua_toboolean(L, index) != 0);
        return true;
    case LUA_TNUMBER:
        if (lua_isinteger(L, index)) {
            out = QVariant::fromValue<qint64>(static_cast<qint64>(lua_tointeger(L, index)));
            return true;
        }
        if (!std::isfinite(double(lua_tonumber(L, index))))
            return false;
        out = QVariant(double(lua_tonumber(L, index)));
        return true;
    case LUA_TSTRING: {
        size_t length = 0;
        const char *raw = lua_tolstring(L, index, &length);
        out = QVariant(QString::fromUtf8(raw, qsizetype(length)));
        return true;
    }
    case LUA_TTABLE:
        return readQmlDataTable(L, index, out, depth + 1);
    default:
        // Functions, userdata and threads have no JSON form.
        return false;
    }
}

// A table with only string keys becomes a map; a sequence 1..n becomes a list; mixed tables are refused.
bool readQmlDataTable(lua_State *L, int index, QVariant &out, int depth)
{
    // Each nesting level pushes a key and a value; grow the stack beyond LUA_MINSTACK.
    luaL_checkstack(L, 3, "QML element data nested too deeply");
    if (depth > kQmlDataMaxDepth)
        return false;
    const int table = lua_absindex(L, index);
    const lua_Integer length = lua_Integer(lua_rawlen(L, table));
    QVariantMap map;
    lua_Integer count = 0;
    lua_pushnil(L);
    while (lua_next(L, table) != 0) {
        ++count;
        if (length == 0) {
            if (lua_type(L, -2) != LUA_TSTRING) {
                lua_pop(L, 2);
                return false;
            }
            QVariant value;
            if (!readQmlDataValue(L, -1, value, depth)) {
                lua_pop(L, 2);
                return false;
            }
            map.insert(QString::fromUtf8(lua_tostring(L, -2)), value);
        }
        lua_pop(L, 1);
    }
    if (length == 0) {
        out = map;
        return true;
    }
    if (count != length)
        return false;
    QVariantList list;
    for (lua_Integer i = 1; i <= length; ++i) {
        lua_rawgeti(L, table, i);
        QVariant value;
        const bool ok = readQmlDataValue(L, -1, value, depth);
        lua_pop(L, 1);
        if (!ok)
            return false;
        list << value;
    }
    out = list;
    return true;
}

}

%}

%typemap(in) const QVariantMap &qmlData (QVariantMap parsed) {
    if (!lua_isnil(L, $input)) {
        QVariant value;
        if (!lua_istable(L, $input) || !readQmlDataTable(L, $input, value, 0)
            || value.userType() != QMetaType::QVariantMap) {
            SWIG_Lua_pusherrstring(L, "QML element data must be a table with string keys holding nil, booleans, numbers, strings or tables");
            SWIG_fail;
        }
        parsed = value.toMap();
    }
    $1 = &parsed;
}

%typecheck(SWIG_TYPECHECK_POINTER) const QVariantMap &qmlData {
    $1 = lua_istable(L, $input) || lua_isnil(L, $input);
}

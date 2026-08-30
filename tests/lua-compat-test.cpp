#include <QList>

#include "util.h"

#include "lua.hpp"

#include <QByteArray>
#include <QDebug>

static bool evaluateBoolean(lua_State *state, const char *script)
{
    if (luaL_dostring(state, script) != LUA_OK) {
        qCritical().noquote() << "Lua compatibility fixture failed:"
                              << lua_tostring(state, -1);
        lua_pop(state, 1);
        return false;
    }
    const bool result = lua_toboolean(state, -1);
    lua_pop(state, 1);
    return result;
}

static QByteArray seededTableTraversal(lua_State *state)
{
    if (luaL_dostring(state, R"lua(
        local values = {}
        for index = 1, 64 do
            values["seeded_key_" .. index] = index
        end
        local order = {}
        for key in pairs(values) do
            order[#order + 1] = key
        end
        return table.concat(order, ",")
    )lua") != LUA_OK) {
        qCritical().noquote() << "Lua seeded traversal fixture failed:"
                              << lua_tostring(state, -1);
        lua_pop(state, 1);
        return {};
    }

    size_t length = 0;
    const char *value = lua_tolstring(state, -1, &length);
    const QByteArray result(value, int(length));
    lua_pop(state, 1);
    return result;
}

int runLuaCompatibilityTests()
{
    const quint64 seed = Q_UINT64_C(0x12345678abcdef01);
    lua_State *first = CreateLuaState(seed);
    lua_State *second = CreateLuaState(seed);
    if (!first || !second) {
        if (first)
            lua_close(first);
        if (second)
            lua_close(second);
        qCritical() << "Unable to create seeded Lua 5.4 states";
        return 1;
    }

    const bool compatible = evaluateBoolean(first, R"lua(
        local sum = 0
        for value = 1, 5 do
            if value % 2 == 0 then continue end
            sum = sum + value
        end

        package.preload["compat.fixture"] = function(...)
            module(..., package.seeall)
            answer = math.floor(42.75)
        end
        local loaded = require("compat.fixture")

        return _VERSION == "Lua 5.4"
            and sum == 9
            and tostring(42.0) == "42"
            and bit32.band() == 0xffffffff
            and bit32.band(-1, 0x80000000) == 0x80000000
            and bit32.band(0x1ffffffff, 0xf0f0f0f0) == 0xf0f0f0f0
            and bit32.lshift == nil
            and loaded == compat.fixture
            and loaded.answer == 42
            and loaded._M == loaded
            and loaded._NAME == "compat.fixture"
            and loaded._PACKAGE == "compat."
            and unpack == nil
            and loadstring == nil
            and package.loaders == nil
            and table.maxn == nil
            and math.log10 == nil
            and setfenv == nil
            and getfenv == nil
    )lua");
    const QByteArray firstOrder = seededTableTraversal(first);
    const QByteArray secondOrder = seededTableTraversal(second);
    lua_close(first);
    lua_close(second);

    if (!compatible) {
        qCritical() << "Lua 5.4 or its minimum Lua 5.2 compatibility surface failed";
        return 2;
    }
    if (firstOrder.isEmpty() || firstOrder != secondOrder) {
        qCritical() << "Seeded Lua table traversal was not stable";
        return 3;
    }
    return 0;
}

#include "util.h"
#include "lua.hpp"
#include "card.h"
#include "game-rng.h"

#include <QCoreApplication>
#include <QDebug>
#include <QRegularExpression>
#include <cmath>
#if !defined(QSAN_ENGINE_BUILD)
#include <QMessageBox>
#endif

extern "C" {
    int luaopen_sgs(lua_State *);
}

namespace {

const char kLua52Compatibility[] = R"lua(
do
	local global_table = _G
	local package_table = package
	local rawget_fn = rawget
	local rawset_fn = rawset
	local type_fn = type
	local error_fn = error
	local select_fn = select
	local string_gmatch = string.gmatch
	local string_match = string.match
	local debug_getinfo = debug.getinfo
	local debug_getupvalue = debug.getupvalue
	local debug_setupvalue = debug.setupvalue
	local getmetatable_fn = getmetatable
	local setmetatable_fn = setmetatable

	local bit32_table = rawget_fn(global_table, "bit32")
	if type_fn(bit32_table) ~= "table" then
		bit32_table = {}
		rawset_fn(global_table, "bit32", bit32_table)
	end
	if rawget_fn(bit32_table, "band") == nil then
		local tointeger = math.tointeger
		rawset_fn(bit32_table, "band", function(...)
			local result = 0xffffffff
			for index = 1, select_fn("#", ...) do
				local value = tointeger(select_fn(index, ...))
				if value == nil then
					error_fn("bad argument #" .. index
						.. " to 'band' (number has no integer representation)", 2)
				end
				result = result & value
			end
			return result & 0xffffffff
		end)
	end

	local function publish_module(name, value)
		local parts = {}
		for part in string_gmatch(name, "[^%.]+") do
			parts[#parts + 1] = part
		end
		if #parts == 0 then
			error_fn("invalid module name", 3)
		end
		local parent = global_table
		for index = 1, #parts - 1 do
			local child = rawget_fn(parent, parts[index])
			if child == nil then
				child = {}
				rawset_fn(parent, parts[index], child)
			elseif type_fn(child) ~= "table" then
				error_fn("name conflict for module '" .. name .. "'", 3)
			end
			parent = child
		end
		rawset_fn(parent, parts[#parts], value)
	end

	local function legacy_module(name, ...)
		if type_fn(name) ~= "string" then
			error_fn("bad argument #1 to 'module' (string expected)", 2)
		end
		local value = package_table.loaded[name]
		if type_fn(value) ~= "table" then
			value = {}
			package_table.loaded[name] = value
		end
		publish_module(name, value)
		if rawget_fn(value, "_NAME") == nil then
			rawset_fn(value, "_M", value)
			rawset_fn(value, "_NAME", name)
			rawset_fn(value, "_PACKAGE", string_match(name, "^(.*%.)") or "")
		end

		-- Lua 5.2 module() changed the caller chunk environment. In 5.4 the
		-- same environment is the caller's _ENV upvalue.
		local caller = debug_getinfo(2, "f").func
		local upvalue_index = 1
		while true do
			local upvalue_name = debug_getupvalue(caller, upvalue_index)
			if upvalue_name == nil then
				error_fn("module() caller has no _ENV upvalue", 2)
			end
			if upvalue_name == "_ENV" then
				debug_setupvalue(caller, upvalue_index, value)
				break
			end
			upvalue_index = upvalue_index + 1
		end

		for option_index = 1, select_fn("#", ...) do
			select_fn(option_index, ...)(value)
		end
		return value
	end

	rawset_fn(global_table, "module", legacy_module)
	rawset_fn(package_table, "seeall", function(value)
		if type_fn(value) ~= "table" then
			error_fn("bad argument #1 to 'seeall' (table expected)", 2)
		end
		local metatable = getmetatable_fn(value)
		if metatable == nil then
			metatable = {}
			setmetatable_fn(value, metatable)
		end
		rawset_fn(metatable, "__index", global_table)
	end)
end
)lua";

unsigned int luaHashSeed(quint64 seed)
{
    return unsigned(seed) ^ unsigned(seed >> 32);
}

int luaGameRandom(lua_State *L)
{
    const lua_Number random = lua_Number(qsanRandomBounded(0x7fffffff))
        / lua_Number(0x7fffffff);
    switch (lua_gettop(L)) {
    case 0:
        lua_pushnumber(L, random);
        break;
    case 1: {
        const lua_Number upper = luaL_checknumber(L, 1);
        luaL_argcheck(L, lua_Number(1) <= upper, 1, "interval is empty");
        lua_pushnumber(L, std::floor(random * upper) + lua_Number(1));
        break;
    }
    case 2: {
        const lua_Number lower = luaL_checknumber(L, 1);
        const lua_Number upper = luaL_checknumber(L, 2);
        luaL_argcheck(L, lower <= upper, 2, "interval is empty");
        lua_pushnumber(L, std::floor(random * (upper - lower + 1)) + lower);
        break;
    }
    default:
        return luaL_error(L, "wrong number of arguments");
    }
    return 1;
}

int luaGameRandomSeed(lua_State *L)
{
    (void)luaL_checkinteger(L, 1);
    return 0;
}

void installGameRandom(lua_State *L)
{
    lua_getglobal(L, "math");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
    }
    lua_pushcfunction(L, &luaGameRandom);
    lua_setfield(L, -2, "random");
    lua_pushcfunction(L, &luaGameRandomSeed);
    lua_setfield(L, -2, "randomseed");
    lua_pop(L, 1);
}

bool installLua52Compatibility(lua_State *L)
{
    if (luaL_dostring(L, kLua52Compatibility) == LUA_OK)
        return true;

    const char *error = lua_tostring(L, -1);
    qCritical().noquote() << "Unable to install Lua 5.2 compatibility:"
                          << (error ? error : "unknown error");
    lua_pop(L, 1);
    return false;
}

}

QVariant GetValueFromLuaState(lua_State *L, const char *table_name, const char *key)
{
    lua_getglobal(L, table_name);
    lua_getfield(L, -1, key);

    QVariant data;
    switch (lua_type(L, -1)) {
    case LUA_TSTRING: {
        data = QString::fromUtf8(lua_tostring(L, -1));
        lua_pop(L, 1);
        break;
    }
    case LUA_TNUMBER: {
        data = lua_tonumber(L, -1);
        lua_pop(L, 1);
        break;
    }
    case LUA_TTABLE: {
        lua_rawgeti(L, -1, 1);
        bool isArray = !lua_isnil(L, -1);
        lua_pop(L, 1);

        if (isArray) {
            QStringList list;

            size_t size = lua_rawlen(L, -1);
            for (size_t i = 0; i < size; i++) {
                lua_rawgeti(L, -1, i + 1);
                QString element = QString::fromUtf8(lua_tostring(L, -1));
                lua_pop(L, 1);
                list << element;
            }
            data = list;
        } else {
            QVariantMap map;
            int t = lua_gettop(L);
            for (lua_pushnil(L); lua_next(L, t); lua_pop(L, 1)) {
                const char *key = lua_tostring(L, -2);
                const char *value = lua_tostring(L, -1);
                map[key] = value;
            }
            data = map;
        }
    }
    default:
        break;
    }

    lua_pop(L, 1);
    return data;
}

lua_State *CreateLuaState()
{
    lua_State *L = luaL_newstate();
    if (!L)
    {
        return nullptr;
    }
    luaL_openlibs(L);
    if (!installLua52Compatibility(L)) {
        lua_close(L);
        return nullptr;
    }
    luaopen_sgs(L);
    return L;
}

lua_State *CreateLuaState(quint64 seed)
{
    lua_State *L = luaL_newstate_seeded(luaHashSeed(seed));
    if (!L)
        return nullptr;
    luaL_openlibs(L);
    if (!installLua52Compatibility(L)) {
        lua_close(L);
        return nullptr;
    }
    installGameRandom(L);
    luaopen_sgs(L);
    return L;
}

lua_State *CreateLuaState(LuaAllocatorFunction allocator, void *userData)
{
    lua_State *L = lua_newstate(allocator, userData);
    if (!L)
        return nullptr;
    luaL_openlibs(L);
    if (!installLua52Compatibility(L)) {
        lua_close(L);
        return nullptr;
    }
    luaopen_sgs(L);
    return L;
}

lua_State *CreateLuaState(LuaAllocatorFunction allocator, void *userData, quint64 seed)
{
    lua_State *L = lua_newstate_seeded(allocator, userData, luaHashSeed(seed));
    if (!L)
        return nullptr;
    luaL_openlibs(L);
    if (!installLua52Compatibility(L)) {
        lua_close(L);
        return nullptr;
    }
    installGameRandom(L);
    luaopen_sgs(L);
    return L;
}

bool DoLuaScript(lua_State *L, const char *script)
{
    if (luaL_dofile(L, script)!=0) {
        QString error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
        // A modal dialog makes headless test failures invisible and leaves the
        // process waiting forever.  Preserve GUI feedback outside headless mode.
        if (qApp && (qApp->arguments().contains("--headless")
            || qApp->arguments().contains("--lua-test")))
            qCritical().noquote() << "Lua script error:" << script << error_msg;
        else
#if defined(QSAN_ENGINE_BUILD)
            qCritical().noquote() << "Lua script error:" << script << error_msg;
#else
            QMessageBox::critical(nullptr, QObject::tr("Lua script error"), error_msg);
#endif
        return false;
    }
    return true;
}

QStringList ListI2S(const QList<int> &intlist)
{
    QStringList stringlist;
	foreach (int n, intlist)
        stringlist << QString::number(n);
    return stringlist;
}

QList<int> ListS2I(const QStringList &stringlist)
{
	bool ok;
    QList<int> intlist;
	foreach (QString st, stringlist) {
		int n = st.toInt(&ok);
        if (ok) intlist << n;
    }
    return intlist;
}

QVariantList ListI2V(const QList<int> &intlist)
{
    QVariantList variantlist;
	foreach (int n, intlist)
        variantlist << QVariant(n);
    return variantlist;
}

QList<int> ListV2I(const QVariantList &variantlist)
{
	bool ok;
    QList<int> intlist;
	foreach (QVariant v, variantlist) {
		int n = v.toInt(&ok);
		if(ok) intlist << n;
    }
    return intlist;
}

bool isNormalGameMode(const QString &mode)
{
    static const QRegularExpression modeRegex("^(0[2-9]|10)p[dz]*$");
    return modeRegex.match(mode).hasMatch();
}

bool isHegemonyGameMode(const QString &mode)
{
    return mode.startsWith("hegemony");
}

DummyCard* dummyCard(const QList<int> &ids)
{
    DummyCard*dc = new DummyCard(ids);
	dc->deleteLater();
    return dc;
}

DummyCard* dummyCard(const QList<const Card*> &cards)
{
    DummyCard*dc = new DummyCard;
	dc->addSubcards(cards);
	dc->deleteLater();
    return dc;
}



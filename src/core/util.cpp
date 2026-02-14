#include "util.h"
#include "lua.hpp"
#include "card.h"

extern "C" {
    int luaopen_sgs(lua_State *);
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
    luaL_openlibs(L);
    luaopen_sgs(L);
    return L;
}

bool DoLuaScript(lua_State *L, const char *script)
{
    if (luaL_dofile(L, script)!=0) {
        QString error_msg = lua_tostring(L, -1);
		lua_pop(L, 1);
        QMessageBox::critical(nullptr, QObject::tr("Lua script error"), error_msg);
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
    static QRegExp moderx("(0[2-9]|10)p[dz]*");
    return moderx.exactMatch(mode);
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



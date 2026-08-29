#ifndef _UTIL_H
#define _UTIL_H

#include "game-rng.h"

#include <cstddef>

struct lua_State;
class QVariant;
class DummyCard;
class Card;

template<typename T>
void qsanShuffle(QList<T> &list)
{
    const int count = list.length();
    for (int index = 0; index < count; ++index)
        list.swapItemsAt(index, qsanRandomBounded(count - index) + index);
}

// lua interpreter related
typedef void *(*LuaAllocatorFunction)(void *, void *, size_t, size_t);
lua_State *CreateLuaState();
lua_State *CreateLuaState(quint64 seed);
lua_State *CreateLuaState(LuaAllocatorFunction allocator, void *userData);
lua_State *CreateLuaState(LuaAllocatorFunction allocator, void *userData, quint64 seed);
bool DoLuaScript(lua_State *L, const char *script);

QVariant GetValueFromLuaState(lua_State *L, const char *table_name, const char *key);

QStringList ListI2S(const QList<int> &intlist);
QList<int> ListS2I(const QStringList &stringlist);
QVariantList ListI2V(const QList<int> &intlist);
QList<int> ListV2I(const QVariantList &variantlist);

bool isNormalGameMode(const QString &mode);
bool isHegemonyGameMode(const QString &mode);

DummyCard* dummyCard(const QList<int> &ids = QList<int>());
DummyCard* dummyCard(const QList<const Card*> &cards);

static const int S_EQUIP_AREA_LENGTH = 5;
static const int S_CARD_TYPE_LENGTH = 4;

#endif

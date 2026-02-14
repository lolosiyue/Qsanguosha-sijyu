#ifndef _UTIL_H
#define _UTIL_H

struct lua_State;
class QVariant;
class DummyCard;
class Card;

template<typename T>
void qShuffle(QList<T> &list)
{
    int n = list.length();
    for (int i = 0; i < n; i++)
        list.swapItemsAt(i, qrand()%(n-i)+i);
}

// lua interpreter related
lua_State *CreateLuaState();
bool DoLuaScript(lua_State *L, const char *script);

QVariant GetValueFromLuaState(lua_State *L, const char *table_name, const char *key);

QStringList ListI2S(const QList<int> &intlist);
QList<int> ListS2I(const QStringList &stringlist);
QVariantList ListI2V(const QList<int> &intlist);
QList<int> ListV2I(const QVariantList &variantlist);

bool isNormalGameMode(const QString &mode);

DummyCard* dummyCard(const QList<int> &ids = QList<int>());
DummyCard* dummyCard(const QList<const Card*> &cards);

static const int S_EQUIP_AREA_LENGTH = 5;
static const int S_CARD_TYPE_LENGTH = 4;

#endif
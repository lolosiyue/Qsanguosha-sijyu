#include "card.h"
#include "ai.h"
#include "engine-bootstrap.h"
#include "general.h"
#include "lua-wrapper.h"
#include "package.h"
#include "player.h"
#include "room.h"
#include "skill.h"
#include "standard.h"

#include <QDebug>
#include <QMetaEnum>
#include <QtGlobal>

class Player;
// engine 側的 Self 放喺 QSanEngine namespace，避免同 GUI 的 `ClientPlayer *Self`
// 在 Itanium ABI（GCC／Clang）下撞名；詳見 src/client/clientplayer.h。
namespace QSanEngine {
extern Player *Self;
}
using QSanEngine::Self;
void setEngineSelf(Player *player);

namespace {

bool hasEnumKey(const QMetaObject &metaObject, const char *enumName, const char *key)
{
    const int enumIndex = metaObject.indexOfEnumerator(enumName);
    if (enumIndex < 0)
        return false;
    bool ok = false;
    metaObject.enumerator(enumIndex).keyToValue(key, &ok);
    return ok;
}

}

int runEnumReflectionTests()
{
    const bool reflected =
        hasEnumKey(Card::staticMetaObject, "Suit", "Spade")
        && hasEnumKey(Card::staticMetaObject, "CardType", "TypeBasic")
        && hasEnumKey(Card::staticMetaObject, "HandlingMethod", "MethodUse")
        && hasEnumKey(General::staticMetaObject, "Gender", "Male")
        && hasEnumKey(Player::staticMetaObject, "Phase", "Play")
        && hasEnumKey(Player::staticMetaObject, "Place", "PlaceHand")
        && hasEnumKey(Player::staticMetaObject, "Role", "Lord")
        && hasEnumKey(Skill::staticMetaObject, "Frequency", "Compulsory")
        && hasEnumKey(Skill::staticMetaObject, "LimitScope", "Limit_Game")
        && hasEnumKey(TargetModSkill::staticMetaObject, "ModType", "ExtraTarget")
        && hasEnumKey(AI::staticMetaObject, "Relation", "Friend")
        && hasEnumKey(Room::staticMetaObject, "GuanxingType", "GuanxingBothSides")
        && hasEnumKey(Package::staticMetaObject, "Type", "GeneralPack")
        && hasEnumKey(EquipCard::staticMetaObject, "Location", "WeaponLocation");
    if (!reflected) {
        qCritical() << "Qt enum reflection metadata is incomplete";
        return 1;
    }
    return 0;
}

int runEngineSelfBridgeTests()
{
    Player *expected = reinterpret_cast<Player *>(quintptr(1));
    setEngineSelf(expected);
    if (Self != expected) {
        qCritical() << "engine Self did not follow the GUI-selected player";
        return 1;
    }

    setEngineSelf(nullptr);
    if (Self != nullptr) {
        qCritical() << "engine Self did not clear with the GUI-selected player";
        return 2;
    }

    qInfo() << "engine Self bridge Axe regression passed";
    return 0;
}

int runCardParseTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }

    const Card *card = Card::Parse(QStringLiteral("@KurouCard=."));
    if (card == nullptr) {
        qCritical() << "Card::Parse(@KurouCard=.) returned nullptr";
        return 2;
    }

    // cloneSkillCard 以 metaObject className 識別卡牌，未必寫入 objectName
    if (card->getClassName() != QStringLiteral("KurouCard")) {
        qCritical() << "Card::Parse(@KurouCard=.) className" << card->getClassName();
        return 3;
    }

    qInfo() << "Card::Parse(@KurouCard=.) regression passed";

    // 無效 LuaSkillCard 字串不得對 nullptr 呼叫 deleteLater（宿敵自動用牌 client 閃退）
    if (Card::Parse(QStringLiteral("##notARealLuaSkillCard[no_suit:0]:.:")) != nullptr) {
        qCritical() << "Card::Parse(unknown ##LuaSkillCard) should return nullptr";
        return 4;
    }

    LuaSkillCard proto(QStringLiteral("#establishOECard"), QStringLiteral("establishOECard"));
    const QString encoded = proto.toString();
    if (encoded.startsWith(QStringLiteral("##"))) {
        qCritical() << "LuaSkillCard::toString double hash" << encoded;
        return 5;
    }
    const Card *oeCard = Card::Parse(encoded);
    if (oeCard == nullptr) {
        qCritical() << "Card::Parse(#establishOECard) failed" << encoded;
        return 6;
    }
    if (oeCard->objectName() != QStringLiteral("#establishOECard")) {
        qCritical() << "Card::Parse(#establishOECard) objectName" << oeCard->objectName();
        return 7;
    }

    const Card *legacyDoubleHash = Card::Parse(QStringLiteral("##establishOECard[no_suit:0]:.:"));
    if (legacyDoubleHash == nullptr) {
        qCritical() << "Card::Parse(##establishOECard legacy) failed";
        return 8;
    }

    qInfo() << "Card::Parse LuaSkillCard #objectName regression passed";
    return 0;
}

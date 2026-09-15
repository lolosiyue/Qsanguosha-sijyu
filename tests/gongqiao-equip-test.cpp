#include "engine-bootstrap.h"
#include "engine.h"
#include "room-test-access.h"
#include "room.h"
#include "serverplayer.h"
#include "skill.h"
#include "wrapped-card.h"

#include <QCoreApplication>
#include <QDebug>

// Regression: GongqiaoCard (th_majun) turns a hand card into a "_zhizhe_*"
// equip and moves it into the equip area. The wrapped card was taken over
// before the move, but the move itself re-filters cards entering an area
// (Room::updateCardsChange -> filterCards(refilter=true)), which resets the
// wrapped card to the engine card. The #zhizhe filter that should re-apply the
// equip only reads the "ZhizheFilter_<id>" room tag, and GongqiaoCard::use set
// that tag after the move. A non-equip card therefore reached
// ServerPlayer::addCard(PlaceEquip) and qobject_cast<const EquipCard *> gave
// nullptr (20p network soak SIGSEGV, 2026-09-14).
namespace {

bool expect(bool condition, const char *context)
{
    if (!condition)
        qCritical() << "gongqiao-equip:" << context;
    return condition;
}

ServerPlayer *addSeat(Room &room, const QString &objectName)
{
    ServerPlayer *player = RoomTestAccess::addOrdinaryPlayer(room, objectName, true);
    player->setState(QStringLiteral("robot"));
    player->drainAllLocks();
    player->releaseLock(ServerPlayer::SEMA_MUTEX);
    return player;
}

int findBasicCard()
{
    for (int i = 0; i < Sanguosha->getCardCount(); ++i) {
        const Card *card = Sanguosha->getEngineCard(i);
        if (card && card->isKindOf("BasicCard"))
            return i;
    }
    return -1;
}

bool gongqiaoTurnsABasicCardIntoAnArmor()
{
    Room room(nullptr, QStringLiteral("02_1v1"));
    EngineRuntimeContextScope scope(*Sanguosha, &room);
    room.roomRuntime()->state().reset();

    ServerPlayer *owner = addSeat(room, QStringLiteral("sgs1"));
    addSeat(room, QStringLiteral("sgs2"));
    RoomTestAccess::resetAlive(room);
    RoomTestAccess::attachThread(room);

    const int cardId = findBasicCard();
    if (!expect(cardId >= 0, "fixture: the engine has a basic card"))
        return false;
    owner->addCard(cardId, Player::PlaceHand);
    room.setCardMapping(cardId, owner, Player::PlaceHand);

    room.registerTestOverride(owner, QStringLiteral("choice"), QStringLiteral("gongqiao"),
                              QStringLiteral("EquipArea1"));

    SkillCard *gongqiao = Sanguosha->cloneSkillCard(QStringLiteral("GongqiaoCard"));
    if (!expect(gongqiao != nullptr, "GongqiaoCard is registered"))
        return false;
    gongqiao->addSubcard(cardId);
    QList<ServerPlayer *> targets;
    gongqiao->use(&room, owner, targets);
    delete gongqiao;

    bool ok = true;
    ok &= expect(room.getCardPlace(cardId) == Player::PlaceEquip,
                 "the card ends up in the equip area");
    ok &= expect(owner->getEquipsId().contains(cardId), "the owner holds the card as an equip");
    const WrappedCard *wrapped = Sanguosha->getWrappedCard(cardId);
    ok &= expect(wrapped && wrapped->getRealCard()->isKindOf("EquipCard"),
                 "the equipped card stays an EquipCard after the move re-filters it");
    ok &= expect(wrapped && wrapped->isKindOf("Armor"), "EquipArea1 turns the card into an armor");
    ok &= expect(room.getTag(QStringLiteral("gongqiaoEquip")).toStringList()
                     == QStringList{QString::number(cardId)},
                 "gongqiaoEquip records the converted card");
    return ok;
}

} // namespace

int runGongqiaoEquipTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }
    if (!gongqiaoTurnsABasicCardIntoAnArmor())
        return 2;
    qInfo() << "gongqiao equip conversion passed";
    return 0;
}

#include "card-lifetime-manager.h"
#include "card.h"
#include "engine-bootstrap.h"
#include "game-snapshot.h"
#include "room.h"
#include "room-test-access.h"
#include "structs.h"

#include <QDebug>
#include <QJsonDocument>
#include <QVariant>

namespace {

bool expect(bool condition, const char *context)
{
    if (condition)
        return true;
    qCritical() << "game snapshot tag test failed:" << context;
    return false;
}

// Room tags carry self-leasing structs: a "UseHistory..." tag holds a
// CardUseStruct that owns a transient Card. The room worker retires those Cards
// on its way out (RoomRuntime::finalizeWorker -> finalizeWorkerDomain), revoking
// the struct's lease whether or not the struct is still alive. A snapshot lives
// far past that point -- it hangs off Room::m_snapshotService -- so capturing
// tags verbatim leaves it holding a freed Card, which ~Room then hands to
// Card::deleteLater().
bool snapshotDropsCardOwningRoomTags()
{
    Room room(nullptr, QStringLiteral("03_1v2"));

    const QString tagKey = QStringLiteral("UseHistorySnapshotProbe");
    bool ok = true;
    {
        CardUseStruct use;
        use.setOwnedCard(new DummyCard);
        room.setTag(tagKey, QVariant::fromValue(use));
        ok &= expect(room.getTag(tagKey).canConvert<CardUseStruct>(),
                     "fixture: the room tag must hold the card-owning struct");

        GameSnapshot snapshot(&room);
        const QVariant captured = snapshot.getState().roomTags.value(tagKey);
        ok &= expect(!captured.canConvert<CardUseStruct>(),
                     "snapshot must not co-own the card-carrying room tag");
    }
    room.removeTag(tagKey);
    return ok;
}

// The capture may drop card ownership, but it must not change what a replay
// file ends up containing: save() writes the tags through QJsonDocument, which
// already renders a CardUseStruct as null.
bool snapshotKeepsSerializedTagsUnchanged()
{
    Room room(nullptr, QStringLiteral("03_1v2"));

    bool ok = true;
    {
        CardUseStruct use;
        use.setOwnedCard(new DummyCard);
        room.setTag(QStringLiteral("UseHistorySnapshotProbe"), QVariant::fromValue(use));
        room.setTag(QStringLiteral("SwapPile"), QVariant(3));
        room.setTag(QStringLiteral("NamesProbe"), QVariant(QStringList{QStringLiteral("sgs1")}));

        const QVariantMap tags = room.getAllTags();
        const GameSnapshot snapshot(&room);
        ok &= expect(QJsonDocument::fromVariant(snapshot.getState().roomTags)
                         == QJsonDocument::fromVariant(tags),
                     "captured tags must serialize exactly like the room's own tags");
    }
    room.removeTag(QStringLiteral("UseHistorySnapshotProbe"));
    room.removeTag(QStringLiteral("SwapPile"));
    room.removeTag(QStringLiteral("NamesProbe"));
    return ok;
}

// GameRule writes ComboMovesCard onto the player on every CardUsed: a
// CardTagOwner wrapping a card the server cloned itself. That value cannot
// cross the JSON boundary; until it is stripped it makes every turn snapshot
// after the first turn ineligible, so save() returns false right away and
// takeover/replay are left with turn_001 as the only node.
bool snapshotStaysEligibleWithAComboMovesTag()
{
    Room room(nullptr, QStringLiteral("03_1v2"));
    ServerPlayer *player = RoomTestAccess::addPlayer(room, QStringLiteral("sgs1"));

    const QString tagKey = QStringLiteral("ComboMovesCard");
    Card *owned = new DummyCard;
    player->setTag(tagKey, QVariant::fromValue(CardTagOwner{owned}));

    bool ok = expect(player->getTag(tagKey).canConvert<CardTagOwner>(),
                     "fixture: the player tag must hold the card-owning wrapper");
    {
        const GameSnapshot snapshot(&room);
        const GlobalSnapshot state = snapshot.getState();
        // A bare shell of a fixture room is not eligible by itself (no real
        // cards, no Lua runtime), so do not assert eligible directly; assert
        // instead that no unsupported reason caused by ComboMovesCard is added.
        for (const QString &reason : state.unsupportedState) {
            ok &= expect(!reason.contains(tagKey),
                         qPrintable(QStringLiteral("ComboMovesCard must not make a "
                                                   "snapshot ineligible: %1").arg(reason)));
        }
        bool sawPlayer = false;
        for (const PlayerSnapshot &captured : state.players) {
            if (captured.objectName != QLatin1String("sgs1"))
                continue;
            sawPlayer = true;
            ok &= expect(!captured.tags.contains(tagKey),
                         "the volatile card tag must be dropped, not captured");
        }
        ok &= expect(sawPlayer, "fixture: the player must appear in the snapshot");
    }
    player->removeTag(tagKey);
    delete owned;
    return ok;
}

// The four read sites (Juchui / ThJizhanmc / ThZhuitao in tenyear.cpp, and
// QVariant::toCard() in swig/qvariant.i) all use value<const Card*>(). Without
// a registered converter QVariant silently returns nullptr, the skill never
// triggers, and nothing reports an error.
bool cardTagOwnerConvertsToACardPointer()
{
    Card *owned = new DummyCard;
    const QVariant wrapped = QVariant::fromValue(CardTagOwner{owned});
    const bool ok = expect(wrapped.value<const Card *>() == owned,
                           "CardTagOwner must convert to const Card*")
        && expect(wrapped.value<Card *>() == owned,
                  "CardTagOwner must convert to Card*");
    delete owned;
    return ok;
}

} // namespace

int runGameSnapshotTagsTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }

    if (!snapshotDropsCardOwningRoomTags())
        return 2;
    if (!snapshotKeepsSerializedTagsUnchanged())
        return 3;
    if (!snapshotStaysEligibleWithAComboMovesTag())
        return 4;
    if (!cardTagOwnerConvertsToACardPointer())
        return 5;

    qInfo() << "game snapshot tag ownership passed";
    return 0;
}

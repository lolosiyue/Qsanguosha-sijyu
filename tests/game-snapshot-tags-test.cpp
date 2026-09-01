#include "card-lifetime-manager.h"
#include "card.h"
#include "engine-bootstrap.h"
#include "game-snapshot.h"
#include "room.h"
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

    qInfo() << "game snapshot tag ownership passed";
    return 0;
}

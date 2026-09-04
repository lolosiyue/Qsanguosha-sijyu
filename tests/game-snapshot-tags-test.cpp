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

// GameRule 喺每次 CardUsed 都會喺玩家身上寫 ComboMovesCard: 一個 CardTagOwner
// 包住 server 自己 clone 出嚟嘅牌。呢個值過唔到 JSON 邊界, 未剔走之前令第一
// 回合之後每一個 turn snapshot 都變 ineligible, save() 一開頭就 return false,
// takeover/replay 只剩得 turn_001 一個節點。
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
        // 一個淨係得個殼嘅 fixture room 本身就唔 eligible (冇實體牌、冇 Lua
        // runtime), 所以唔可以直接斷言 eligible; 要斷言嘅係「唔會多咗一個因
        // ComboMovesCard 而起嘅 unsupported 理由」。
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

// 四個讀取點 (tenyear.cpp 的 Juchui / ThJizhanmc / ThZhuitao, 同 swig/qvariant.i
// 的 QVariant::toCard()) 一律用 value<const Card*>()。冇註冊 converter 嘅話
// QVariant 會靜靜地還 nullptr, 技能唔會發動亦唔會報錯。
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

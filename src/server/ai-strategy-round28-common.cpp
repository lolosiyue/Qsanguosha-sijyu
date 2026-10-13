#include "ai-strategy-round28-common.h"
#include "ai-strategy-room-ledger.h"
#include "room.h"
#include "serverplayer.h"
#include <QObject>
#include <QPointer>
#include <QHash>

namespace {
struct SlotWitness {
    QPointer<ServerPlayer> player;
    QString publicName;
};
// Room-child lifetime; no process-global map, cross-room thread data, native
// card/userdata retention, or gameplay revision mutation.
class GeneralSlotLedger final : public AIRoomLedger {
public:
    explicit GeneralSlotLedger(Room &room) : AIRoomLedger(room) {
        setObjectName(QStringLiteral("isolated-ai-round28-public-general-slots"));
    }
    QHash<QString, SlotWitness> deputy;
};
GeneralSlotLedger *ledger(Room &room, bool create) {
    auto *object=room.findChild<QObject *>(QStringLiteral("isolated-ai-round28-public-general-slots"),Qt::FindDirectChildrenOnly);
    auto *result=dynamic_cast<GeneralSlotLedger *>(object);
    if (!result && create && !object) result=new GeneralSlotLedger(room);
    return result;
}
bool rosterMember(Room &room, ServerPlayer *player) {
    return player && player->getRoom()==&room && room.getAllPlayers(true).contains(player);
}
}

void recordAIRound28PublicGeneralSlot(Room &room, ServerPlayer *player,
                                     const char *property, const QString &value) {
    if (!property || QString::fromLatin1(property)!=QStringLiteral("general2")
        || !rosterMember(room,player)) return;
    auto *state=ledger(room,true);
    if (!state) return;
    const QString sent=value.isEmpty() ? player->property(property).toString() : value;
    state->deputy.insert(player->objectName(), SlotWitness{player,sent});
}

QJsonValue aiRound28ShownAllGenerals(Room &room, ServerPlayer *viewer,
                                   ServerPlayer *player) {
    if (!rosterMember(room,viewer) || !rosterMember(room,player)) return QJsonValue(QJsonValue::Undefined);
    if (viewer==player) return player->hasShownAllGenerals();
    if (!player->hasShownGeneral()) return false;
    if (player->hasShownGeneral2()) return true;
    const auto *state=ledger(room,false);
    if (!state) return QJsonValue(QJsonValue::Undefined);
    const auto it=state->deputy.constFind(player->objectName());
    if (it==state->deputy.cend() || it->player!=player || it->publicName.isEmpty())
        return QJsonValue(QJsonValue::Undefined);
    // Equality validates already-public existence; no new foreign face is
    // returned. Nonempty general2 makes (actual_general2 ? actual : general2)
    // nonnull regardless of the private actual slot. Public empty alone cannot
    // prove the private slot absent and is deliberately insufficient for true.
    if (!player->getGeneral2() || player->getGeneral2Name()!=it->publicName)
        return QJsonValue(QJsonValue::Undefined);
    return false;
}

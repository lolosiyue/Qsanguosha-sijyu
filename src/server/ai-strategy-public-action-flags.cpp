#include "ai-strategy-public-action-flags.h"

#include "card.h"
#include "room.h"
#include "serverplayer.h"
#include "wrapped-card.h"

#include <QHash>
#include <QObject>
#include <QSet>

namespace {
const QStringList &auditedNames()
{
    static const QStringList names{"Qinggang", "SlashIgnoreArmor", "nosjiefan-slash"};
    return names;
}

struct FlagWitness {
    bool value = false;
    QSet<QString> audience;
};

class PublicActionFlagsLedger final : public QObject {
public:
    explicit PublicActionFlagsLedger(Room &room) : QObject(&room) {}
    QHash<const Card *, QHash<QString, FlagWitness>> cards;
    QSet<const Card *> watched;
};

PublicActionFlagsLedger *ledgerFor(Room &room, bool create)
{
    const QString key = QStringLiteral("isolated-ai-public-action-flag-ledger");
    auto *object = room.findChild<QObject *>(key, Qt::FindDirectChildrenOnly);
    if (!object && create) {
        object = new PublicActionFlagsLedger(room);
        object->setObjectName(key);
    }
    return static_cast<PublicActionFlagsLedger *>(object);
}

bool isActualPhysicalCard(Room &room, const Card *card)
{
    return card && !card->isVirtualCard() && card->getId() >= 0
        && card->getEffectiveId() == card->getId()
        && room.getCard(card->getId()) == card;
}
}

void recordAIStrategyPublicCardFlag(Room &room, const Card *card,
                                   const QString &operation)
{
    if (!isActualPhysicalCard(room, card)) return;
    const bool clearAll = operation == QStringLiteral(".");
    const bool remove = operation.startsWith('-');
    const QString name = remove ? operation.mid(1) : operation;
    if (!clearAll && !auditedNames().contains(name)) return;
    auto *ledger = ledgerFor(room, true);
    if (!ledger->watched.contains(card)) {
        ledger->watched.insert(card);
        QObject::connect(card, &QObject::destroyed, ledger, [ledger, card]() {
            ledger->cards.remove(card);
            ledger->watched.remove(card);
        }, Qt::DirectConnection);
        if (const auto *wrapped = qobject_cast<const WrappedCard *>(card)) {
            QObject::connect(wrapped, &WrappedCard::identityChanged, ledger,
                [ledger, card]() { ledger->cards.remove(card); }, Qt::DirectConnection);
        }
    }
    QSet<QString> audience;
    for (const ServerPlayer *player : room.getAllPlayers(true))
        audience.insert(player->objectName());
    auto &witnesses = ledger->cards[card];
    const QStringList names = clearAll ? auditedNames() : QStringList{name};
    for (const QString &flag : names) {
        const bool value = !clearAll && !remove;
        // A callback during notification could have changed the flag. Such a
        // mismatch never reveals its new value and cannot establish a witness.
        if (card->hasFlag(flag) != value) { witnesses.remove(flag); continue; }
        witnesses.insert(flag, FlagWitness{value, audience});
    }
}

QJsonObject readAIStrategyPublicCardFlags(Room &room, ServerPlayer *viewer,
                                        const Card *card)
{
    QJsonObject result;
    if (!viewer || viewer->getRoom() != &room || !card) return result;
    auto *ledger = ledgerFor(room, false);
    if (!ledger) return result;
    auto found = ledger->cards.find(card);
    if (found == ledger->cards.end()) return result;
    // There must be a prior public witness before checking the live wrapper.
    // No ID lookup, flag enumeration, face or private value leaves this method.
    if (!isActualPhysicalCard(room, card)) { ledger->cards.erase(found); return result; }
    auto &witnesses = found.value();
    for (auto it = witnesses.begin(); it != witnesses.end();) {
        if (!it.value().audience.contains(viewer->objectName())) { ++it; continue; }
        if (card->hasFlag(it.key()) != it.value().value) {
            it = witnesses.erase(it);
            continue;
        }
        result.insert(it.key(), it.value().value);
        ++it;
    }
    return result;
}

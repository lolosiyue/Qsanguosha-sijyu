#include "ai-strategy-public-disclosure.h"

#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "roomthread.h"
#include "serverplayer.h"
#include "structs.h"
#include "wrapped-card.h"
#include "couple-scenario.h"

#include <QJsonArray>
#include <QObject>

namespace {
QObject *publicLedger(Room &room, bool create)
{
    auto *ledger = room.findChild<QObject *>(QStringLiteral("isolated-ai-public-move-ledger"),
                                          Qt::FindDirectChildrenOnly);
    if (!ledger && create) {
        ledger = new QObject(&room);
        ledger->setObjectName(QStringLiteral("isolated-ai-public-move-ledger"));
    }
    return ledger;
}

bool publicDestination(Player::Place place)
{
    return place == Player::PlaceTable || place == Player::DiscardPile
        || place == Player::PlaceEquip || place == Player::PlaceJudge;
}
QJsonObject publicFace(const Card *card, int id)
{
    return QJsonObject{{"id", id}, {"effective_id", id},
                {"name", card->objectName()}, {"class_name", card->getClassName()},
                {"engine_class_name", QString::fromLatin1(card->metaObject()->className())},
                {"kind_of", QJsonArray::fromStringList(card->getKindOfNames())},
                {"subtype", card->getSubtype()}, {"suit", int(card->getSuit())},
                {"number", card->getNumber()}, {"type_id", int(card->getTypeId())},
                {"red", card->isRed()}, {"black", card->isBlack()},
                {"virtual_card", false}, {"subcards", QJsonArray()},
                {"legacy_string_key", QString::number(id)}, {"legacy_sort_key", QString::number(id)}};
}

}

void recordAIStrategyPublicCoupleLog(Room &room, const LogMessage &log)
{
    if (!dynamic_cast<const CoupleScenario *>(room.getScenario()) || !log.from
        || log.from->getRoom() != &room || log.to.size() != 1 || !log.to.first()
        || log.to.first()->getRoom() != &room || log.from == log.to.first()
        || (log.type != "#Marry" && log.type != "#Divorse")) return;
    auto *ledger = publicLedger(room, true);
    auto spouses = ledger->property("public_spouses").toJsonObject();
    const QString from = log.from->objectName(), to = log.to.first()->objectName();
    spouses.insert(from, log.type == "#Marry" ? QJsonValue(to) : QJsonValue(false));
    spouses.insert(to, log.type == "#Marry" ? QJsonValue(from) : QJsonValue(false));
    ledger->setProperty("public_spouses", spouses);
}

void recordAIStrategyPublicKaiyuanFlag(Room &room, ServerPlayer *player, const QString &operation)
{
    if (!player || player->getRoom() != &room || (operation != "KaiyuanShengshi"
        && operation != "-KaiyuanShengshi" && operation != ".")) return;
    auto *ledger = publicLedger(room, true);
    auto flags = ledger->property("public_kaiyuan_flags").toJsonObject();
    const bool observed = operation == "KaiyuanShengshi";
    if (player->hasFlag("KaiyuanShengshi") == observed) flags.insert(player->objectName(), observed);
    else flags.insert(player->objectName(), QJsonValue());
    ledger->setProperty("public_kaiyuan_flags", flags);
}

QJsonValue readAIStrategyPublicKaiyuanFlag(Room &room, ServerPlayer *player)
{
    if (!player || player->getRoom() != &room) return QJsonValue();
    const auto *ledger = publicLedger(room, false);
    const auto flags = ledger ? ledger->property("public_kaiyuan_flags").toJsonObject() : QJsonObject();
    const auto value = flags.contains(player->objectName()) ? flags.value(player->objectName()) : QJsonValue(false);
    if (!value.isBool() || player->hasFlag("KaiyuanShengshi") != value.toBool()) return QJsonValue();
    return value;
}

QJsonObject readAIStrategyPublicSpouse(Room &room, ServerPlayer *viewer)
{
    const QJsonObject unknown{{"known", false}};
    const auto *scenario = dynamic_cast<const CoupleScenario *>(room.getScenario());
    if (!scenario || !viewer || viewer->getRoom() != &room) return unknown;
    const auto *ledger = publicLedger(room, false);
    const auto spouses = ledger ? ledger->property("public_spouses").toJsonObject() : QJsonObject();
    // Scenario construction starts without marriages. An unwitnessed positive
    // tag never establishes a relationship, including during nested callbacks.
    const QJsonValue observed = spouses.value(viewer->objectName()).isUndefined()
        ? QJsonValue(false) : spouses.value(viewer->objectName());
    const auto *current = scenario->getSpouse(viewer);
    if (current ? !observed.isString() || observed.toString() != current->objectName()
                : observed != QJsonValue(false)) return unknown;
    return {{"known", true}, {"spouse", observed}};
}

void invalidateAIStrategyPublicDisclosure(Room &room, int cardId)
{
    auto *ledger = publicLedger(room, false);
    if (!ledger) return;
    auto rows = ledger->property("cards").toJsonObject();
    rows.remove(QString::number(cardId));
    ledger->setProperty("cards", rows);
}

void recordAIStrategyPublicMoves(Room &room, const QList<CardsMoveStruct> &moves,
                                bool publiclyVisible)
{
    // Room::notifyMoveCards sets recipient.open = visible || isRelevant(player).
    // move.open is therefore NOT an all-viewer disclosure proof. The argument
    // here must be the real committed movement's explicit visible argument.
    if (!publiclyVisible) return;
    auto *ledger = publicLedger(room, true);
    auto rows = ledger->property("cards").toJsonObject();
    const QString revision = QString::number(room.roomRuntime()->stateRevision());
    QJsonArray audience;
    for (const auto *player : room.getAllPlayers(true)) audience.append(player->objectName());
    for (const auto &move : moves) {
        if (!publicDestination(move.to_place)) continue;
        for (int id : move.card_ids) {
            // Confirm the completed destination after nested install callbacks.
            // In particular, an unknown mapping defaults to Table in the native
            // index; only a genuine completed move may reach this code.
            if (id < 0 || room.getCardPlace(id) != move.to_place
                || room.getCardOwner(id) != move.to) continue;
            const Card *card = room.getCard(id);
            if (!card || card->isVirtualCard() || card->getEffectiveId() != id) continue;
            // Public face primitives only. No flags, tags, skill/source IDs,
            // userString, availability, private limitations or action ticket.
            const QJsonObject face = publicFace(card, id);
            auto watched = ledger->property("watched").toJsonObject();
            const QString key = QString::number(id);
            if (!watched.contains(key)) {
                if (const auto *wrapped = qobject_cast<const WrappedCard *>(card)) {
                    QObject::connect(wrapped, &WrappedCard::identityChanged, ledger,
                        [&room, id]() { invalidateAIStrategyPublicDisclosure(room, id); },
                        Qt::DirectConnection);
                    watched.insert(key, true);
                    ledger->setProperty("watched", watched);
                }
            }
            rows.insert(QString::number(id), QJsonObject{{"known", true}, {"card", face},
                {"place", int(move.to_place)}, {"owner", move.to ? move.to->objectName() : QString()},
                {"revision", revision}, {"provenance", "completed_public_card_move"},
                {"audience", audience}});
        }
    }
    ledger->setProperty("cards", rows);
}

QJsonObject readAIStrategyPublicDisclosure(Room &room, ServerPlayer *viewer, int cardId)
{
    const QJsonObject unknown{{"known", false}};
    if (!viewer || viewer->getRoom() != &room || cardId < 0) return unknown;
    auto *ledger = publicLedger(room, false);
    if (!ledger) return unknown;
    auto row = ledger->property("cards").toJsonObject().value(QString::number(cardId)).toObject();
    if (!row.value("known").toBool()
        || !row.value("audience").toArray().contains(viewer->objectName())) return unknown;
    // A real witness must precede all location/face reads. Mapping writes
    // invalidate it synchronously, so private moves cannot expose their new
    // destination or wrapper. Unrelated tag/revision updates do not lose an
    // already public face (notifyMoveToPile itself updates a tag).
    const auto stale = [&]() { invalidateAIStrategyPublicDisclosure(room, cardId); return unknown; };
    const auto place = Player::Place(row.value("place").toInt(-1));
    if (!publicDestination(place) || room.getCardPlace(cardId) != place) return stale();
    const auto *owner = room.getCardOwner(cardId);
    if ((owner ? owner->objectName() : QString()) != row.value("owner").toString()) return stale();
    const Card *card = room.getCard(cardId);
    if (!card || card->isVirtualCard() || card->getEffectiveId() != cardId
        || publicFace(card, cardId) != row.value("card").toObject()) return stale();
    row.remove("audience");
    return row;
}

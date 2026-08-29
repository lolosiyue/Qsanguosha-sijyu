#include "card-movement-service.h"

#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "roomthread.h"
#include "settings.h"
#include "standard.h"

#include <algorithm>
#include <functional>

using namespace QSanProtocol;

namespace {

bool compareByActionOrderOneTime(CardsMoveOneTimeStruct move1,
                                 CardsMoveOneTimeStruct move2)
{
    Player *a = move1.from;
    Player *b = move2.from;
    if (a == nullptr) a = move1.to;
    if (b == nullptr) b = move2.to;

    if (a == nullptr || b == nullptr) return a != nullptr;
    if (a == b) return false;
    ServerPlayer *sa = static_cast<ServerPlayer *>(a);
    return sa->getRoom()->getFront(sa, static_cast<ServerPlayer *>(b)) == sa;
}

bool compareByActionOrder(CardsMoveStruct move1, CardsMoveStruct move2)
{
    Player *a = move1.from;
    Player *b = move2.from;
    if (a == nullptr) a = move1.to;
    if (b == nullptr) b = move2.to;

    if (a == nullptr || b == nullptr) return a != nullptr;
    if (a == b) return false;
    ServerPlayer *sa = static_cast<ServerPlayer *>(a);
    return sa->getRoom()->getFront(sa, static_cast<ServerPlayer *>(b)) == sa;
}

}

void CardLocationIndex::set(int cardId, ServerPlayer *owner, Player::Place place)
{
    m_owners.insert(cardId, owner);
    m_places.insert(cardId, place);
}

ServerPlayer *CardLocationIndex::owner(int cardId) const
{
    return m_owners.value(cardId, nullptr);
}

Player::Place CardLocationIndex::place(int cardId) const
{
    if (cardId < 0) return Player::PlaceUnknown;
    return m_places.value(cardId, Player::PlaceTable);
}

CardMovementService::CardMovementService(Room &room)
    : m_room(room), m_drawPile(&m_pile1), m_discardPile(&m_pile2)
{
}

QList<int> &CardMovementService::drawPile()
{
    return *m_drawPile;
}

const QList<int> &CardMovementService::drawPile() const
{
    return *m_drawPile;
}

QList<int> &CardMovementService::discardPile()
{
    return *m_discardPile;
}

const QList<int> &CardMovementService::discardPile() const
{
    return *m_discardPile;
}

QList<int> &CardMovementService::tableCards()
{
    return m_tableCards;
}

const QList<int> &CardMovementService::tableCards() const
{
    return m_tableCards;
}

QList<int> &CardMovementService::primaryPile()
{
    return m_pile1;
}

void CardMovementService::setCardMapping(int cardId, ServerPlayer *owner,
                                         Player::Place place)
{
    m_locations.set(cardId, owner, place);
}

ServerPlayer *CardMovementService::getCardOwner(int cardId) const
{
    return m_locations.owner(cardId);
}

Player::Place CardMovementService::getCardPlace(int cardId) const
{
    return m_locations.place(cardId);
}

QList<int> CardMovementService::getNCards(int n, bool updatePileNumber, bool isTop)
{
    QList<int> cardIds;
    for (int i = 0; i < n; ++i)
        cardIds << drawCard(isTop);

    if (!cardIds.isEmpty())
        m_room.m_runtime->advanceStateRevision(RoomRuntime::CardsMoved);

    if (updatePileNumber)
        m_room.doBroadcastNotify(S_COMMAND_UPDATE_PILE, m_drawPile->length());

    return cardIds;
}

int CardMovementService::drawCard(bool isTop)
{
    m_room.getThread()->trigger(FetchDrawPileCard, &m_room, nullptr);
    if (m_drawPile->isEmpty()) swapPile();
    return isTop ? m_drawPile->takeFirst() : m_drawPile->takeLast();
}

void CardMovementService::swapPile()
{
    if (m_discardPile->isEmpty())
        m_room.gameOver(".");

    const int times = m_room.getTag("SwapPile").toInt() + 1;
    m_room.setTag("SwapPile", times);

    QVariant data = times;
    foreach (ServerPlayer *player, m_room.getAllPlayers())
        m_room.getThread()->trigger(SwapPile, &m_room, player, data);

    int limit = Config.value("PileSwappingLimitation", 5).toInt() + 1;
    if (m_room.getMode() == "04_1v3")
        limit = qMin(limit, Config.BanPackages.contains("maneuvering") ? 3 : 2);
    else if (m_room.getMode() == "08_defense")
        limit = qMin(limit, Config.BanPackages.contains("maneuvering") ? 9 : 6);
    if (limit > 0 && times >= limit)
        m_room.gameOver(".");

    qSwap(m_drawPile, m_discardPile);

    m_room.doBroadcastNotify(S_COMMAND_RESET_PILE, data);
    m_room.doBroadcastNotify(S_COMMAND_UPDATE_PILE, m_drawPile->length());

    qsanShuffle(*m_drawPile);
    foreach (int cardId, *m_drawPile) {
        setCardMapping(cardId, nullptr, Player::DrawPile);
        m_room.clearCardFlag(cardId);
    }

    foreach (ServerPlayer *player, m_room.getAllPlayers())
        m_room.getThread()->trigger(SwappedPile, &m_room, player, data);
}

int CardMovementService::getCardFromPile(const QString &cardPattern)
{
    if (m_drawPile->isEmpty())
        swapPile();

    if (cardPattern.startsWith("@")) {
        if (cardPattern == "@duanliang") {
            foreach (int cardId, *m_drawPile) {
                const Card *card = Sanguosha->getCard(cardId);
                if (card->isBlack()
                    && (card->isKindOf("BasicCard") || card->isKindOf("EquipCard")))
                    return cardId;
            }
        }
    } else {
        foreach (int cardId, *m_drawPile) {
            if (Sanguosha->getCard(cardId)->objectName() == cardPattern)
                return cardId;
        }
    }

    return -1;
}

void CardMovementService::returnToTopDrawPile(QList<int> cards)
{
    while (!cards.isEmpty()) {
        const int id = cards.takeLast();
        m_drawPile->removeAll(id);
        setCardMapping(id, nullptr, Player::DrawPile);
        m_drawPile->prepend(id);
    }
    m_room.doBroadcastNotify(S_COMMAND_UPDATE_PILE, m_drawPile->length());
}

void CardMovementService::returnToEndDrawPile(QList<int> cards)
{
    while (!cards.isEmpty()) {
        const int id = cards.takeLast();
        m_drawPile->removeAll(id);
        setCardMapping(id, nullptr, Player::DrawPile);
        m_drawPile->append(id);
    }
    m_room.doBroadcastNotify(S_COMMAND_UPDATE_PILE, m_drawPile->length());
}

QList<int> CardMovementService::drawCardsList(ServerPlayer *player, int n,
                                              const QString &reason,
                                              bool isTop, bool visible)
{
    if (n < 1 || (!player->isAlive() && reason != "reform"))
        return QList<int>();

    DrawStruct draw;
    draw.who = player;
    draw.num = n;
    draw.reason = reason;
    draw.top = isTop;
    draw.visible = visible;
    QVariant data = QVariant::fromValue(draw);
    m_room.getThread()->trigger(DrawNCards, &m_room, draw.who, data);
    draw = data.value<DrawStruct>();
    if (draw.num < 1 || !draw.who->isAlive()) return QList<int>();

    CardsMoveStruct move;
    move.card_ids = getNCards(draw.num, false, draw.top);
    move.open = draw.visible;
    move.from = nullptr;
    move.to = draw.who;
    move.to_place = Player::PlaceHand;
    move.reason = CardMoveReason(CardMoveReason::S_REASON_DRAW,
                                 draw.who->objectName(), reason, "");
    moveCardsAtomic(move, visible, false);
    draw.card_ids = move.card_ids;
    data.setValue(draw);
    m_room.getThread()->trigger(AfterDrawNCards, &m_room, draw.who, data);

    return move.card_ids;
}

void CardMovementService::drawCards(ServerPlayer *player, int n,
                                    const QString &reason, bool isTop, bool visible)
{
    drawCards(QList<ServerPlayer *>() << player, n, reason, isTop, visible);
}

void CardMovementService::drawCards(QList<ServerPlayer *> players, int n,
                                    const QString &reason, bool isTop, bool visible)
{
    drawCards(players, QList<int>() << n, reason, isTop, visible);
}

void CardMovementService::drawCards(QList<ServerPlayer *> players, QList<int> nList,
                                    const QString &reason, bool isTop, bool visible)
{
    QVariantList datas;
    QList<CardsMoveStruct> moves;
    for (int i = 0; i < players.length(); ++i) {
        DrawStruct draw;
        draw.who = players[i];
        if (!draw.who->isAlive() && reason != "reform") continue;
        draw.num = i < nList.length() ? nList[i] : nList.last();
        if (draw.num < 1) continue;
        draw.reason = reason;
        draw.top = isTop;
        draw.visible = visible;
        QVariant data = QVariant::fromValue(draw);
        m_room.getThread()->trigger(DrawNCards, &m_room, draw.who, data);
        draw = data.value<DrawStruct>();
        if (draw.num < 1 || !draw.who->isAlive()) continue;

        CardsMoveStruct move;
        move.card_ids = getNCards(draw.num, false, draw.top);
        move.open = draw.visible;
        move.to = draw.who;
        move.to_place = Player::PlaceHand;
        move.reason = CardMoveReason(CardMoveReason::S_REASON_DRAW,
                                     draw.who->objectName(), reason, "");
        moves.append(move);

        draw.card_ids = move.card_ids;
        datas << QVariant::fromValue(draw);
    }
    moveCardsAtomic(moves, visible, false);

    for (int i = 0; i < moves.length(); ++i)
        m_room.getThread()->trigger(AfterDrawNCards, &m_room,
                                    static_cast<ServerPlayer *>(moves[i].to), datas[i]);
}

void CardMovementService::obtainCard(ServerPlayer *target, const Card *card,
                                     const CardMoveReason &reason, bool visible)
{
    moveCardTo(card, target, Player::PlaceHand, reason, visible, false);
}

void CardMovementService::obtainCard(ServerPlayer *target, const Card *card, bool visible)
{
    obtainCard(target, card, "", visible);
}

void CardMovementService::obtainCard(ServerPlayer *target, int cardId, bool visible)
{
    obtainCard(target, Sanguosha->getCard(cardId), visible);
}

void CardMovementService::obtainCard(ServerPlayer *target, const Card *card,
                                     const QString &skillName, bool visible)
{
    CardMoveReason reason(CardMoveReason::S_REASON_GOTBACK,
                          target->objectName(), skillName, "");
    ServerPlayer *from = getCardOwner(card->getEffectiveId());
    if (from) {
        reason.m_reason = CardMoveReason::S_REASON_EXTRACTION;
        reason.m_targetId = from->objectName();
    }
    reason.m_extraData = QVariant::fromValue(card);
    obtainCard(target, card, reason, visible);
}

void CardMovementService::obtainCard(ServerPlayer *target, int cardId,
                                     const QString &skillName, bool visible)
{
    obtainCard(target, Sanguosha->getCard(cardId), skillName, visible);
}

void CardMovementService::recastCard(ServerPlayer *player, const Card *card,
                                     const QString &skillName)
{
    recastCardWithDraw(player, card, 1, skillName);
}

void CardMovementService::recastCard(ServerPlayer *player, int cardId,
                                     const QString &skillName)
{
    recastCardWithDraw(player, cardId, 1, skillName);
}

void CardMovementService::recastCards(ServerPlayer *player,
                                      const QList<int> &cardIds,
                                      const QString &skillName)
{
    if (!player || cardIds.isEmpty()) return;

    QList<int> validIds;
    foreach (int id, cardIds) {
        if (id >= 0 && Sanguosha->getCard(id))
            validIds << id;
    }

    recastCardsWithDraw(player, validIds, validIds.length(), skillName);
}

void CardMovementService::recastCardWithDraw(ServerPlayer *player, const Card *card,
                                             int drawCount,
                                             const QString &skillName)
{
    if (!card || !player) return;

    player->broadcastSkillInvoke("@recast");

    LogMessage log;
    log.type = "$RecastCard";
    log.from = player;
    log.card_str = card->toString();
    m_room.sendLog(log);

    const QString finalSkillName = skillName.isEmpty()
        ? card->getSkillName() : skillName;
    CardMoveReason reason(CardMoveReason::S_REASON_RECAST,
                          player->objectName(), finalSkillName, "");
    moveCardTo(card, player, nullptr, Player::DiscardPile, reason, true, false);

    if (drawCount > 0)
        drawCards(player, drawCount, "recast", true, false);
}

void CardMovementService::recastCardWithDraw(ServerPlayer *player, int cardId,
                                             int drawCount,
                                             const QString &skillName)
{
    if (!player || cardId < 0) return;
    const Card *card = Sanguosha->getCard(cardId);
    if (!card) return;
    recastCardWithDraw(player, card, drawCount, skillName);
}

void CardMovementService::recastCardsWithDraw(ServerPlayer *player,
                                              const QList<int> &cardIds,
                                              int drawCount,
                                              const QString &skillName)
{
    if (!player || cardIds.isEmpty()) return;

    QList<int> validIds;
    foreach (int id, cardIds) {
        if (id >= 0 && Sanguosha->getCard(id))
            validIds << id;
    }
    if (validIds.isEmpty()) return;

    player->broadcastSkillInvoke("@recast");
    DummyCard dummy(validIds);

    LogMessage log;
    log.type = "$RecastCard";
    log.from = player;
    log.card_str = dummy.toString();
    m_room.sendLog(log);

    CardMoveReason reason(CardMoveReason::S_REASON_RECAST,
                          player->objectName(), skillName, "");
    CardsMoveStruct move(validIds, player, nullptr, Player::PlaceUnknown,
                         Player::DiscardPile, reason);
    moveCardsAtomic(move, true, false);

    if (drawCount > 0)
        drawCards(player, drawCount, "recast", true, false);
}

void CardMovementService::throwCard(const Card *card, ServerPlayer *who,
                                    ServerPlayer *thrower)
{
    CardMoveReason reason(CardMoveReason::S_REASON_THROW,
                          who ? who->objectName() : "");
    if (thrower) {
        reason.m_reason = CardMoveReason::S_REASON_DISMANTLE;
        reason.m_targetId = who ? who->objectName() : "";
        reason.m_playerId = thrower->objectName();
    }
    reason.m_extraData = QVariant::fromValue(card);
    reason.m_skillName = card->getSkillName();
    throwCard(card, reason, who, thrower);
}

void CardMovementService::throwCard(const Card *card, const CardMoveReason &reason,
                                    ServerPlayer *who, ServerPlayer *thrower)
{
    throwCard(card->getSubcards(), reason, who, thrower);
}

void CardMovementService::throwCard(QList<int> cardIds,
                                    const CardMoveReason &reason,
                                    ServerPlayer *who, ServerPlayer *thrower)
{
    if (cardIds.isEmpty()) return;

    LogMessage log;
    log.type = "$EnterDiscardPile";
    if (who) {
        log.type = "$DiscardCard";
        log.from = who;
        if (thrower) {
            log.type = "$DiscardCardByOther";
            log.from = thrower;
            log.to << who;
        }
    }
    log.card_str = ListI2S(cardIds).join("+");
    m_room.sendLog(log);
    moveCardsAtomic(CardsMoveStruct(cardIds, who, nullptr, Player::PlaceUnknown,
                                    Player::DiscardPile, reason), true, false);
}

void CardMovementService::throwCard(int cardId, ServerPlayer *who,
                                    ServerPlayer *thrower)
{
    throwCard(QList<int>() << cardId, "", who, thrower);
}

void CardMovementService::throwCard(int cardId, const QString &skillName,
                                    ServerPlayer *who, ServerPlayer *thrower)
{
    throwCard(QList<int>() << cardId, skillName, who, thrower);
}

void CardMovementService::throwCard(const Card *card, const QString &skillName,
                                    ServerPlayer *who, ServerPlayer *thrower)
{
    CardMoveReason reason(CardMoveReason::S_REASON_THROW,
                          who ? who->objectName() : "", skillName, "");
    reason.m_extraData = QVariant::fromValue(card);
    if (thrower) {
        reason.m_reason = CardMoveReason::S_REASON_DISMANTLE;
        reason.m_targetId = who ? who->objectName() : "";
        reason.m_playerId = thrower->objectName();
    }
    throwCard(card, reason, who, thrower);
}

void CardMovementService::throwCard(QList<int> cardIds,
                                    const QString &skillName,
                                    ServerPlayer *who, ServerPlayer *thrower)
{
    CardMoveReason reason(CardMoveReason::S_REASON_THROW,
                          who ? who->objectName() : "", skillName, "");
    if (thrower) {
        reason.m_reason = CardMoveReason::S_REASON_DISMANTLE;
        reason.m_targetId = who ? who->objectName() : "";
        reason.m_playerId = thrower->objectName();
    }
    throwCard(cardIds, reason, who, thrower);
}

void CardMovementService::moveCardTo(const Card *card, ServerPlayer *dstPlayer,
                                     Player::Place dstPlace, bool visible,
                                     bool guanxin)
{
    moveCardTo(card, dstPlayer, dstPlace,
               CardMoveReason(CardMoveReason::S_REASON_UNKNOWN, ""),
               visible, guanxin);
}

void CardMovementService::moveCardTo(const Card *card, ServerPlayer *dstPlayer,
                                     Player::Place dstPlace,
                                     const CardMoveReason &reason,
                                     bool visible, bool guanxin)
{
    moveCardTo(card, nullptr, dstPlayer, dstPlace, "", reason, visible, guanxin);
}

void CardMovementService::moveCardTo(const Card *card, ServerPlayer *srcPlayer,
                                     ServerPlayer *dstPlayer,
                                     Player::Place dstPlace,
                                     const CardMoveReason &reason,
                                     bool visible, bool guanxin)
{
    moveCardTo(card, srcPlayer, dstPlayer, dstPlace, "", reason, visible, guanxin);
}

void CardMovementService::moveCardTo(const Card *card, ServerPlayer *srcPlayer,
                                     ServerPlayer *dstPlayer,
                                     Player::Place dstPlace,
                                     const QString &pileName,
                                     const CardMoveReason &reason,
                                     bool visible, bool guanxin)
{
    CardsMoveStruct move;
    if (card->isVirtualCard()) {
        move.card_ids = card->getSubcards();
        if (move.card_ids.isEmpty()) return;
    } else {
        move.card_ids << card->getId();
    }
    move.to = dstPlayer;
    move.to_place = dstPlace;
    move.to_pile_name = pileName;
    move.from = srcPlayer;
    move.reason = reason;
    moveCardsAtomic(move, visible, guanxin);
}

CardMovementService::_MoveSourceClassifier::_MoveSourceClassifier(
    const CardsMoveStruct &move)
    : m_from(move.from), m_from_place(move.from_place),
      m_from_pile_name(move.from_pile_name),
      m_from_player_name(move.from_player_name)
{
}

void CardMovementService::_MoveSourceClassifier::copyTo(CardsMoveStruct &move) const
{
    move.from = m_from;
    move.from_place = m_from_place;
    move.from_pile_name = m_from_pile_name;
    move.from_player_name = m_from_player_name;
}

bool CardMovementService::_MoveSourceClassifier::operator==(
    const _MoveSourceClassifier &other) const
{
    return m_from == other.m_from && m_from_place == other.m_from_place
        && m_from_pile_name == other.m_from_pile_name;
}

bool CardMovementService::_MoveSourceClassifier::operator<(
    const _MoveSourceClassifier &other) const
{
    const std::less<Player *> playerLess;
    if (m_from != other.m_from)
        return playerLess(m_from, other.m_from);
    if (m_from_place != other.m_from_place)
        return m_from_place < other.m_from_place;
    return m_from_pile_name < other.m_from_pile_name;
}

CardMovementService::_MoveMergeClassifier::_MoveMergeClassifier(
    const CardsMoveStruct &move)
    : m_from(move.from), m_to(move.to), m_to_place(move.to_place),
      m_to_pile_name(move.to_pile_name), m_reason(move.reason),
      m_is_last_handcard(move.is_last_handcard)
{
}

bool CardMovementService::_MoveMergeClassifier::operator==(
    const _MoveMergeClassifier &other) const
{
    return m_from == other.m_from && m_to == other.m_to
        && m_to_place == other.m_to_place
        && m_to_pile_name == other.m_to_pile_name;
}

bool CardMovementService::_MoveMergeClassifier::operator<(
    const _MoveMergeClassifier &other) const
{
    const std::less<Player *> playerLess;
    if (m_from != other.m_from)
        return playerLess(m_from, other.m_from);
    if (m_to != other.m_to)
        return playerLess(m_to, other.m_to);
    if (m_to_place != other.m_to_place)
        return m_to_place < other.m_to_place;
    return m_to_pile_name < other.m_to_pile_name;
}

CardMovementService::_MoveSeparateClassifier::_MoveSeparateClassifier(
    const CardsMoveOneTimeStruct &moveOneTime, int index)
    : m_from(moveOneTime.from), m_to(moveOneTime.to),
      m_from_place(moveOneTime.from_places[index]),
      m_to_place(moveOneTime.to_place),
      m_from_pile_name(moveOneTime.from_pile_names[index]),
      m_to_pile_name(moveOneTime.to_pile_name),
      m_open(moveOneTime.open[index]), m_reason(moveOneTime.reason)
{
}

bool CardMovementService::_MoveSeparateClassifier::operator==(
    const _MoveSeparateClassifier &other) const
{
    return m_from == other.m_from && m_to == other.m_to
        && m_from_place == other.m_from_place
        && m_to_place == other.m_to_place
        && m_from_pile_name == other.m_from_pile_name
        && m_to_pile_name == other.m_to_pile_name
        && m_reason == other.m_reason;
}

bool CardMovementService::_MoveSeparateClassifier::operator<(
    const _MoveSeparateClassifier &other) const
{
    const std::less<Player *> playerLess;
    if (m_from != other.m_from)
        return playerLess(m_from, other.m_from);
    if (m_to != other.m_to)
        return playerLess(m_to, other.m_to);
    if (m_from_place != other.m_from_place)
        return m_from_place < other.m_from_place;
    if (m_to_place != other.m_to_place)
        return m_to_place < other.m_to_place;
    if (m_from_pile_name != other.m_from_pile_name)
        return m_from_pile_name < other.m_from_pile_name;
    if (m_to_pile_name != other.m_to_pile_name)
        return m_to_pile_name < other.m_to_pile_name;
    return m_reason < other.m_reason;
}

void CardMovementService::fillMoveInfo(CardsMoveStruct &move, int id) const
{
    ServerPlayer *owner = getCardOwner(id);
    if (owner && move.from != owner) move.from = owner;
    move.from_place = getCardPlace(id);
    if (move.from) {
        move.from_player_name = move.from->objectName();
        if (move.from_place == Player::PlaceSpecial
            || move.from_place == Player::PlaceTable)
            move.from_pile_name = move.from->getPileName(id);
    }
    if (move.to) {
        move.to_player_name = move.to->objectName();
        if (move.to_place == Player::PlaceSpecial
            || move.to_place == Player::PlaceTable)
            move.to_pile_name = move.to->getPileName(id);
    }
}

QList<CardsMoveStruct> CardMovementService::normalizeMoves(
    QList<CardsMoveStruct> cardsMoves)
{
    QList<int> ids;
    QList<CardsMoveStruct> allSubMoves;
    foreach (CardsMoveStruct move, cardsMoves) {
        QMap<_MoveSourceClassifier, QList<int>> moveMap;
        foreach (int id, move.card_ids) {
            if (ids.contains(id)) continue;
            fillMoveInfo(move, id);
            moveMap[_MoveSourceClassifier(move)] << id;
            ids << id;
        }
        foreach (_MoveSourceClassifier classifier, moveMap.keys()) {
            classifier.copyTo(move);
            if (move.from != move.to
                || move.from_place != move.to_place
                || move.from_pile_name != move.to_pile_name) {
                move.card_ids = moveMap[classifier];
                allSubMoves << move;
            }
        }
    }
    return allSubMoves;
}

QList<CardsMoveOneTimeStruct> CardMovementService::mergeMoves(
    QList<CardsMoveStruct> cardsMoves)
{
    QList<CardsMoveOneTimeStruct> result;
    QMap<_MoveMergeClassifier, QList<CardsMoveStruct>> moveMap;
    foreach (CardsMoveStruct move, cardsMoves)
        moveMap[_MoveMergeClassifier(move)].append(move);

    foreach (_MoveMergeClassifier classifier, moveMap.keys()) {
        CardsMoveOneTimeStruct moveOneTime;
        moveOneTime.from = classifier.m_from;
        moveOneTime.to = classifier.m_to;
        moveOneTime.reason = classifier.m_reason;
        moveOneTime.to_place = classifier.m_to_place;
        moveOneTime.to_pile_name = classifier.m_to_pile_name;
        foreach (CardsMoveStruct move, moveMap[classifier]) {
            moveOneTime.card_ids.append(move.card_ids);
            moveOneTime.last_hand_suits << move.last_hand_suits;
            moveOneTime.is_last_handcard = move.is_last_handcard;
            for (int i = 0; i < move.card_ids.length(); ++i) {
                moveOneTime.from_places.append(move.from_place);
                moveOneTime.from_pile_names.append(move.from_pile_name);
                moveOneTime.open.append(move.open);
            }
        }
        result.append(moveOneTime);
    }
    if (result.length() > 1)
        std::sort(result.begin(), result.end(), compareByActionOrderOneTime);
    return result;
}

QList<CardsMoveStruct> CardMovementService::splitMoves(
    QList<CardsMoveOneTimeStruct> moveOneTimes)
{
    QList<CardsMoveStruct> cardMoves;
    QMap<_MoveSeparateClassifier, QList<int>> moveMap;
    foreach (CardsMoveOneTimeStruct moveOneTime, moveOneTimes) {
        for (int i = 0; i < moveOneTime.card_ids.length(); ++i)
            moveMap[_MoveSeparateClassifier(moveOneTime, i)] << moveOneTime.card_ids[i];
    }

    foreach (_MoveSeparateClassifier classifier, moveMap.keys()) {
        CardsMoveStruct cardMove(moveMap[classifier], classifier.m_from,
                                 classifier.m_to, classifier.m_from_place,
                                 classifier.m_to_place, classifier.m_reason);
        if (classifier.m_from)
            cardMove.from_player_name = classifier.m_from->objectName();
        if (classifier.m_to)
            cardMove.to_player_name = classifier.m_to->objectName();
        cardMove.from_pile_name = classifier.m_from_pile_name;
        cardMove.to_pile_name = classifier.m_to_pile_name;
        cardMove.open = classifier.m_open;

        if (classifier.m_from_place == Player::PlaceHand) {
            QList<int> hands = classifier.m_from->handCards();
            foreach (int id, cardMove.card_ids) {
                hands.removeOne(id);
                const QString suit = Sanguosha->getCard(id)->getSuitString();
                if (cardMove.last_hand_suits.contains(suit)) continue;
                cardMove.last_hand_suits << suit;
                foreach (int handId, hands) {
                    if (Sanguosha->getCard(handId)->getSuitString() == suit) {
                        cardMove.last_hand_suits.removeOne(suit);
                        break;
                    }
                }
            }
            cardMove.is_last_handcard = hands.isEmpty();
        }
        cardMoves.append(cardMove);
    }
    if (cardMoves.length() > 1)
        std::sort(cardMoves.begin(), cardMoves.end(), compareByActionOrder);
    return cardMoves;
}

void CardMovementService::moveCardsAtomic(CardsMoveStruct cardsMove,
                                          bool visible, bool guanxing)
{
    moveCardsAtomic(QList<CardsMoveStruct>() << cardsMove, visible, guanxing);
}

void CardMovementService::moveCardsAtomic(QList<CardsMoveStruct> cardsMoves,
                                          bool visible, bool guanxing)
{
    cardsMoves = normalizeMoves(cardsMoves);
    if (cardsMoves.isEmpty()) return;

    QList<CardsMoveStruct> filteredMoves;
    foreach (CardsMoveStruct move, cardsMoves) {
        CardsMoveStruct filteredMove = move;
        filteredMove.card_ids.clear();
        foreach (int id, move.card_ids) {
            if (!move.from) {
                filteredMove.card_ids << id;
                continue;
            }
            bool allowed = true;
            switch (move.to_place) {
            case Player::PlaceHand:
                allowed = move.from->canGet(move.from, id);
                break;
            case Player::PlaceEquip:
            case Player::PlaceDelayedTrick:
            case Player::PlaceJudge:
                allowed = move.from->canMove(move.from, id);
                break;
            default:
                break;
            }
            if (!allowed) continue;
            filteredMove.card_ids << id;
        }
        if (!filteredMove.card_ids.isEmpty())
            filteredMoves << filteredMove;
    }
    cardsMoves = filteredMoves;
    if (cardsMoves.isEmpty()) return;

    QList<CardsMoveOneTimeStruct> moveOneTimes = mergeMoves(cardsMoves);
    for (int i = 0; i < moveOneTimes.length(); ++i) {
        QVariant data = QVariant::fromValue(moveOneTimes[i]);
        foreach (ServerPlayer *player, m_room.getAllPlayers())
            m_room.getThread()->trigger(BeforeCardsMove, &m_room, player, data);
        moveOneTimes[i] = data.value<CardsMoveOneTimeStruct>();
    }
    cardsMoves = splitMoves(moveOneTimes);
    if (cardsMoves.isEmpty()) return;

    m_room.notifyMoveCards(true, cardsMoves, visible);
    foreach (CardsMoveStruct move, cardsMoves) {
        foreach (int id, move.card_ids) {
            if (move.from) move.from->removeCard(id, move.from_place);
            switch (move.from_place) {
            case Player::DiscardPile:
                m_discardPile->removeAll(id);
                break;
            case Player::DrawPile:
                m_drawPile->removeAll(id);
                break;
            case Player::PlaceSpecial:
                m_tableCards.removeAll(id);
                break;
            default:
                break;
            }
            setCardMapping(id, static_cast<ServerPlayer *>(move.to), move.to_place);
        }
        m_room.updateCardsChange(move);
    }

    m_room.notifyMoveCards(false, cardsMoves, visible);
    foreach (CardsMoveStruct move, cardsMoves) {
        foreach (int id, move.card_ids) {
            m_room.clearCardTip(id);
            if (visible)
                m_room.setCardFlag(id, "visible");
            else if (move.from_place != Player::DrawPile)
                m_room.setCardFlag(id, "-visible");
            if (move.to) move.to->addCard(id, move.to_place);
            switch (move.to_place) {
            case Player::DiscardPile:
                m_discardPile->prepend(id);
                break;
            case Player::DrawPile:
                m_drawPile->prepend(id);
                break;
            case Player::PlaceSpecial:
                m_tableCards.append(id);
                break;
            default:
                break;
            }
        }
        if (move.from_place == Player::DrawPile
            || move.to_place == Player::DrawPile) {
            m_room.doBroadcastNotify(S_COMMAND_UPDATE_PILE, m_drawPile->length());
            if (guanxing && move.to_place == Player::DrawPile) {
                ServerPlayer *from = m_room.findChild<ServerPlayer *>(
                    move.reason.m_playerId);
                if (!from) from = static_cast<ServerPlayer *>(move.from);
                if (from && from->isAlive()) {
                    m_room.askForGuanxing(from,
                        getNCards(move.card_ids.length(), false, true),
                        Room::GuanxingUpOnly, false);
                }
            }
        }
    }

    if (cardsMoves.first().reason.m_skillName == "InitialHandCards"
        && cardsMoves.first().reason.m_reason == CardMoveReason::S_REASON_DRAW)
        m_room.askForLuckCard(cardsMoves);

    QList<int> selectedToDiscard;
    QList<int> processedIds;
    QList<CardsMoveStruct> invalidEquipMoves;
    foreach (CardsMoveStruct move, cardsMoves) {
        if (move.to_place != Player::PlaceEquip || !move.to) continue;
        ServerPlayer *target = static_cast<ServerPlayer *>(move.to);
        foreach (int id, move.card_ids) {
            if (processedIds.contains(id)) continue;
            const Card *card = Sanguosha->getCard(id);
            const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
            if (!equip) continue;

            const QList<int> occupyingSlots = equip->getOccupyLocations();
            foreach (int slot, occupyingSlots) {
                if (!target->hasEquipArea(slot)) {
                    target->removeCard(id, Player::PlaceEquip);
                    m_discardPile->prepend(id);
                    setCardMapping(id, nullptr, Player::DiscardPile);
                    // from 必須是原裝備區，否則 client loseCards 對不到 CardItem
                    invalidEquipMoves << CardsMoveStruct(id, target, nullptr,
                        Player::PlaceEquip, Player::DiscardPile,
                        CardMoveReason(CardMoveReason::S_REASON_CHANGE_EQUIP,
                                       target->objectName(), QString(),
                                       "change equip"));
                    processedIds.append(id);
                    break;
                }

                QList<const Card *> slotEquips;
                foreach (const Card *equippedCard, target->getEquips()) {
                    const EquipCard *equipped = qobject_cast<const EquipCard *>(
                        equippedCard->getRealCard());
                    if (equipped && equipped->getOccupyLocations().contains(slot))
                        slotEquips << equippedCard;
                }
                if (slotEquips.length() <= target->getEquipArea(slot)) continue;

                QList<int> equipIds;
                foreach (const Card *equippedCard, slotEquips) {
                    if (!selectedToDiscard.contains(equippedCard->getId()))
                        equipIds << equippedCard->getId();
                }

                if (equipIds.isEmpty()) {
                    target->removeCard(id, Player::PlaceEquip);
                    m_discardPile->prepend(id);
                    setCardMapping(id, nullptr, Player::DiscardPile);
                    // from 必須是原裝備區，否則 client loseCards 對不到 CardItem
                    invalidEquipMoves << CardsMoveStruct(id, target, nullptr,
                        Player::PlaceEquip, Player::DiscardPile,
                        CardMoveReason(CardMoveReason::S_REASON_CHANGE_EQUIP,
                                       target->objectName(), QString(),
                                       "change equip"));
                    processedIds.append(id);
                } else if (equipIds.length() == 1) {
                    const int cardId = equipIds.first();
                    selectedToDiscard.append(cardId);
                    target->removeCard(cardId, Player::PlaceEquip);
                    m_discardPile->prepend(cardId);
                    setCardMapping(cardId, nullptr, Player::DiscardPile);
                    invalidEquipMoves << CardsMoveStruct(cardId, target, nullptr,
                        Player::PlaceEquip, Player::DiscardPile,
                        CardMoveReason(CardMoveReason::S_REASON_CHANGE_EQUIP,
                                       target->objectName(), QString(),
                                       "change equip"));
                } else {
                    const int cardId = m_room.askForCardChosen(
                        target, target, "e",
                        "@replace-equip:" + QString::number(slot), false,
                        Card::MethodDiscard, QList<int>());
                    if (cardId > 0 && !selectedToDiscard.contains(cardId)) {
                        selectedToDiscard.append(cardId);
                        target->removeCard(cardId, Player::PlaceEquip);
                        m_discardPile->prepend(cardId);
                        setCardMapping(cardId, nullptr, Player::DiscardPile);
                        invalidEquipMoves << CardsMoveStruct(cardId, target, nullptr,
                            Player::PlaceEquip, Player::DiscardPile,
                            CardMoveReason(CardMoveReason::S_REASON_CHANGE_EQUIP,
                                           target->objectName(), QString(),
                                           "change equip"));
                    }
                }
            }
        }
    }
    if (!invalidEquipMoves.isEmpty()) {
        // 必須成對：只送 GET 會讓 moveId 變 -1，client getCards 對空 stash takeFirst AV
        m_room.notifyMoveCards(true, invalidEquipMoves, true);
        m_room.notifyMoveCards(false, invalidEquipMoves, true);
    }

    foreach (CardsMoveStruct move, cardsMoves) {
        if (move.to_place == Player::PlaceEquip && move.to) {
            ServerPlayer *target = static_cast<ServerPlayer *>(move.to);
            if (target) target->refreshUIState();
        }
        if (move.from_place == Player::PlaceEquip
            && move.from && move.from != move.to) {
            ServerPlayer *source = static_cast<ServerPlayer *>(move.from);
            if (source) source->refreshUIState();
        }
    }

    m_room.m_runtime->advanceStateRevision(RoomRuntime::CardsMoved);
    moveOneTimes = mergeMoves(cardsMoves);
    foreach (CardsMoveOneTimeStruct moveOneTime, moveOneTimes) {
        QVariant data = QVariant::fromValue(moveOneTime);
        foreach (ServerPlayer *player, m_room.getAllPlayers())
            m_room.getThread()->trigger(CardsMoveOneTime, &m_room, player, data);
    }
}

void CardMovementService::moveCardsToEndOfDrawpile(ServerPlayer *player,
                                                   QList<int> cardIds,
                                                   const QString &skillName,
                                                   bool visible, bool guanxing)
{
    if (cardIds.isEmpty()) return;
    QList<CardsMoveStruct> moves;
    moves << CardsMoveStruct(cardIds, nullptr, Player::DrawPile,
        CardMoveReason(CardMoveReason::S_REASON_PUT_END,
                       player->objectName(), skillName, ""));
    moves = normalizeMoves(moves);
    if (moves.isEmpty()) return;

    QList<CardsMoveOneTimeStruct> moveOneTimes = mergeMoves(moves);
    for (int i = 0; i < moveOneTimes.length(); ++i) {
        QVariant data = QVariant::fromValue(moveOneTimes[i]);
        foreach (ServerPlayer *triggerPlayer, m_room.getAllPlayers())
            m_room.getThread()->trigger(BeforeCardsMove, &m_room,
                                        triggerPlayer, data);
        moveOneTimes[i] = data.value<CardsMoveOneTimeStruct>();
    }
    moves = splitMoves(moveOneTimes);
    if (moves.isEmpty()) return;

    m_room.notifyMoveCards(true, moves, visible);
    foreach (CardsMoveStruct move, moves) {
        foreach (int id, move.card_ids) {
            if (move.from) move.from->removeCard(id, move.from_place);
            switch (move.from_place) {
            case Player::DiscardPile:
                m_discardPile->removeAll(id);
                break;
            case Player::DrawPile:
                m_drawPile->removeAll(id);
                break;
            case Player::PlaceSpecial:
                m_tableCards.removeAll(id);
                break;
            default:
                break;
            }
            setCardMapping(id, static_cast<ServerPlayer *>(move.to), move.to_place);
        }
        m_room.updateCardsChange(move);
    }

    m_room.notifyMoveCards(false, moves, visible);
    foreach (CardsMoveStruct move, moves) {
        foreach (int id, move.card_ids) {
            m_room.clearCardTip(id);
            if (visible)
                m_room.setCardFlag(id, "visible");
            else if (move.from_place != Player::DrawPile)
                m_room.setCardFlag(id, "-visible");
            if (move.to) move.to->addCard(id, move.to_place);
            switch (move.to_place) {
            case Player::DiscardPile:
                m_discardPile->prepend(id);
                break;
            case Player::DrawPile:
                m_drawPile->append(id);
                break;
            case Player::PlaceSpecial:
                m_tableCards.append(id);
                break;
            default:
                break;
            }
        }
        if (move.from_place == Player::DrawPile
            || move.to_place == Player::DrawPile)
            m_room.doBroadcastNotify(S_COMMAND_UPDATE_PILE, m_drawPile->length());
    }

    m_room.m_runtime->advanceStateRevision(RoomRuntime::CardsMoved);
    if (guanxing) {
        foreach (const CardsMoveStruct &move, moves) {
            if (move.to_place != Player::DrawPile) continue;
            ServerPlayer *from = m_room.findChild<ServerPlayer *>(
                move.reason.m_playerId);
            if (!from) from = static_cast<ServerPlayer *>(move.from);
            if (from && from->isAlive()) {
                m_room.askForGuanxing(from,
                    getNCards(move.card_ids.length(), false, false),
                    Room::GuanxingDownOnly, false);
            }
        }
    }

    moveOneTimes = mergeMoves(moves);
    foreach (CardsMoveOneTimeStruct moveOneTime, moveOneTimes) {
        QVariant data = QVariant::fromValue(moveOneTime);
        foreach (ServerPlayer *triggerPlayer, m_room.getAllPlayers())
            m_room.getThread()->trigger(CardsMoveOneTime, &m_room,
                                        triggerPlayer, data);
    }
}

void CardMovementService::moveCardsInToDrawpile(ServerPlayer *player,
                                                const Card *card,
                                                const QString &skillName,
                                                int n, bool visible)
{
    QList<int> cardIds;
    if (card->isVirtualCard())
        cardIds = card->getSubcards();
    else
        cardIds << card->getId();
    moveCardsInToDrawpile(player, cardIds, skillName, n, visible);
}

void CardMovementService::moveCardsInToDrawpile(ServerPlayer *player, int cardId,
                                                const QString &skillName,
                                                int n, bool visible)
{
    moveCardsInToDrawpile(player, QList<int>() << cardId,
                          skillName, n, visible);
}

void CardMovementService::moveCardsInToDrawpile(ServerPlayer *player,
                                                QList<int> cardIds,
                                                const QString &skillName,
                                                int n, bool visible)
{
    if (n <= 0) n = qsanRandomBounded(m_drawPile->length()) + 1;
    if (n >= m_drawPile->length()) {
        moveCardsToEndOfDrawpile(player, cardIds, skillName, visible, false);
        return;
    }

    QList<CardsMoveStruct> moves;
    moves << CardsMoveStruct(cardIds, nullptr, Player::DrawPile,
        CardMoveReason(CardMoveReason::S_REASON_SHUFFLE,
                       player->objectName(), skillName, ""));
    moves = normalizeMoves(moves);
    if (moves.isEmpty()) return;

    QList<CardsMoveOneTimeStruct> moveOneTimes = mergeMoves(moves);
    for (int i = 0; i < moveOneTimes.length(); ++i) {
        QVariant data = QVariant::fromValue(moveOneTimes[i]);
        foreach (ServerPlayer *triggerPlayer, m_room.getAllPlayers())
            m_room.getThread()->trigger(BeforeCardsMove, &m_room,
                                        triggerPlayer, data);
        moveOneTimes[i] = data.value<CardsMoveOneTimeStruct>();
    }
    moves = splitMoves(moveOneTimes);
    if (moves.isEmpty()) return;

    m_room.notifyMoveCards(true, moves, visible);
    foreach (CardsMoveStruct move, moves) {
        foreach (int id, move.card_ids) {
            if (move.from) move.from->removeCard(id, move.from_place);
            switch (move.from_place) {
            case Player::DiscardPile:
                m_discardPile->removeAll(id);
                break;
            case Player::DrawPile:
                m_drawPile->removeAll(id);
                break;
            case Player::PlaceSpecial:
                m_tableCards.removeAll(id);
                break;
            default:
                break;
            }
            setCardMapping(id, static_cast<ServerPlayer *>(move.to), move.to_place);
        }
        m_room.updateCardsChange(move);
    }

    m_room.notifyMoveCards(false, moves, visible);
    foreach (CardsMoveStruct move, moves) {
        foreach (int id, move.card_ids) {
            m_room.clearCardTip(id);
            if (visible)
                m_room.setCardFlag(id, "visible");
            else if (move.from_place != Player::DrawPile)
                m_room.setCardFlag(id, "-visible");
            switch (move.to_place) {
            case Player::DiscardPile:
                m_discardPile->prepend(id);
                break;
            case Player::DrawPile:
                m_drawPile->insert(n - 1, id);
                break;
            case Player::PlaceSpecial:
                m_tableCards.append(id);
                break;
            default:
                break;
            }
        }
    }
    m_room.doBroadcastNotify(S_COMMAND_UPDATE_PILE, m_drawPile->length());
    m_room.m_runtime->advanceStateRevision(RoomRuntime::CardsMoved);

    moveOneTimes = mergeMoves(moves);
    foreach (CardsMoveOneTimeStruct moveOneTime, moveOneTimes) {
        QVariant data = QVariant::fromValue(moveOneTime);
        foreach (ServerPlayer *triggerPlayer, m_room.getAllPlayers())
            m_room.getThread()->trigger(CardsMoveOneTime, &m_room,
                                        triggerPlayer, data);
    }
}

void CardMovementService::shuffleIntoDrawPile(ServerPlayer *player,
                                              QList<int> cardIds,
                                              const QString &skillName,
                                              bool visible)
{
    QList<CardsMoveStruct> moves;
    moves << CardsMoveStruct(cardIds, nullptr, Player::DrawPile,
        CardMoveReason(CardMoveReason::S_REASON_SHUFFLE,
                       player ? player->objectName() : "", skillName, ""));
    moves = normalizeMoves(moves);
    if (moves.isEmpty()) return;

    QList<CardsMoveOneTimeStruct> moveOneTimes = mergeMoves(moves);
    for (int i = 0; i < moveOneTimes.length(); ++i) {
        QVariant data = QVariant::fromValue(moveOneTimes[i]);
        foreach (ServerPlayer *triggerPlayer, m_room.getAllPlayers())
            m_room.getThread()->trigger(BeforeCardsMove, &m_room,
                                        triggerPlayer, data);
        moveOneTimes[i] = data.value<CardsMoveOneTimeStruct>();
    }
    moves = splitMoves(moveOneTimes);
    if (moves.isEmpty()) return;

    m_room.notifyMoveCards(true, moves, visible);
    foreach (CardsMoveStruct move, moves) {
        foreach (int id, move.card_ids) {
            if (move.from) move.from->removeCard(id, move.from_place);
            switch (move.from_place) {
            case Player::DiscardPile:
                m_discardPile->removeAll(id);
                break;
            case Player::DrawPile:
                m_drawPile->removeAll(id);
                break;
            case Player::PlaceSpecial:
                m_tableCards.removeAll(id);
                break;
            default:
                break;
            }
            setCardMapping(id, static_cast<ServerPlayer *>(move.to), move.to_place);
        }
        m_room.updateCardsChange(move);
    }

    m_room.notifyMoveCards(false, moves, visible);
    foreach (CardsMoveStruct move, moves) {
        foreach (int id, move.card_ids) {
            m_room.clearCardTip(id);
            if (visible)
                m_room.setCardFlag(id, "visible");
            else if (move.from_place != Player::DrawPile)
                m_room.setCardFlag(id, "-visible");
            if (move.to) move.to->addCard(id, move.to_place);
            switch (move.to_place) {
            case Player::DiscardPile:
                m_discardPile->prepend(id);
                break;
            case Player::DrawPile:
                m_drawPile->insert(qsanRandomBounded(m_drawPile->length()), id);
                break;
            case Player::PlaceSpecial:
                m_tableCards.append(id);
                break;
            default:
                break;
            }
        }
    }
    m_room.doBroadcastNotify(S_COMMAND_UPDATE_PILE, m_drawPile->length());
    m_room.m_runtime->advanceStateRevision(RoomRuntime::CardsMoved);

    moveOneTimes = mergeMoves(moves);
    foreach (CardsMoveOneTimeStruct moveOneTime, moveOneTimes) {
        QVariant data = QVariant::fromValue(moveOneTime);
        foreach (ServerPlayer *triggerPlayer, m_room.getAllPlayers())
            m_room.getThread()->trigger(CardsMoveOneTime, &m_room,
                                        triggerPlayer, data);
    }
}

void CardMovementService::removeDerivativeCards()
{
    bool removed = false;
    foreach (int id, *m_drawPile) {
        const Card *card = Sanguosha->getEngineCard(id);
        if (card->objectName().startsWith("_")
            || card->property("DerivativeCard").toBool()) {
            setCardMapping(id, nullptr, Player::PlaceTable);
            m_drawPile->removeAll(id);
            removed = true;
        }
    }
    if (removed)
        m_room.m_runtime->advanceStateRevision(RoomRuntime::CardsMoved);
    m_room.doBroadcastNotify(S_COMMAND_UPDATE_PILE, m_drawPile->length());
}

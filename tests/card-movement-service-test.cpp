#include "engine-bootstrap.h"
#include "engine.h"
#include "lua-runtime.h"
#include "protocol/protocol-runtime.h"
#include "room-test-access.h"
#include "room-state.h"
#include "settings.h"

#include <QDebug>
#include <QScopeGuard>

using namespace QSanProtocol;

namespace {

#define MOVE_CHECK(condition) do { if (!(condition)) { \
    qCritical() << "Card movement contract failed at line" << __LINE__ << #condition; \
    return false; \
} } while (false)

struct MoveObservation
{
    TriggerEvent event;
    CardsMoveOneTimeStruct move;
    bool bothHandsEmpty;
};

class MovementProbe : public TriggerSkill
{
public:
    MovementProbe() : TriggerSkill("#movement-service-probe")
    {
        events << BeforeCardsMove << CardsMoveOneTime;
        global = true;
    }

    bool triggerable(const ServerPlayer *) const override { return true; }

    bool trigger(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (player != first) return false;
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.reason.m_skillName != "movement-contract") return false;
        if (event == BeforeCardsMove && redirectBottom) {
            move.to = nullptr;
            move.to_place = Player::DrawPileBottom;
            data = QVariant::fromValue(move);
        }
        observations << MoveObservation{event, move,
            first->isKongcheng() && second->isKongcheng()};
        if (event == CardsMoveOneTime && move.to_place == Player::PlaceTable && interruptLoss)
            throw TurnBroken;
        if (event == CardsMoveOneTime && move.to_place == Player::PlaceTable && killOnLoss)
            killOnLoss->setAlive(false);
        return false;
    }

    ServerPlayer *first = nullptr;
    ServerPlayer *second = nullptr;
    ServerPlayer *killOnLoss = nullptr;
    bool redirectBottom = false;
    bool interruptLoss = false;
    mutable QList<MoveObservation> observations;
};

struct MovePacket
{
    ServerPlayer *receiver;
    int command;
    QVariantMap payload;
};

class MovementFixture
{
public:
    MovementFixture()
        : room(nullptr, "04p"), engineScope(*Sanguosha, &room),
          luaBinding(*room.luaRuntime())
    {
        RoomTestAccess::attachThread(room);
        room.getRoomState()->reset();
        // These focused rooms skip game setup, which normally indexes the deck.
        // Without this, obtainCard treats deck IDs as table cards and reuses them.
        for (int id : room.getDrawPile())
            room.setCardMapping(id, nullptr, Player::DrawPile);
        first = add("movement-first");
        second = add("movement-second");
        observer = add("movement-observer");
        first->setNext(second);
        second->setNext(observer);
        observer->setNext(first);
        RoomTestAccess::resetAlive(room);
        room.setCurrent(first);
        probe = new MovementProbe;
        probe->setParent(room.getThread());
        probe->first = first;
        probe->second = second;
        room.getThread()->addTriggerSkill(probe);
    }

    ServerPlayer *add(const QString &name)
    {
        ServerPlayer *player = RoomTestAccess::addPlayer(room, name, "online");
        auto *ai = new TrustAI(player);
        ai->setParent(player);
        player->setAI(ai);
        player->setGeneralName("sujiang");
        player->setRole("renegade");
        player->setPhase(Player::NotActive);
        player->setSeat(room.getPlayers().size());
        QObject::connect(player, &ServerPlayer::message_ready, player,
                         [this, player](const QByteArray &bytes) {
            ProtocolMessage message;
            if (!ProtocolCodecRouter().decode(bytes, &message).success) {
                decodeFailed = true;
                return;
            }
            if (message.command == S_COMMAND_LOSE_CARD || message.command == S_COMMAND_GET_CARD)
                packets << MovePacket{player, int(message.command), message.payload.toMap()};
        });
        return player;
    }

    QList<int> give(ServerPlayer *player, int count)
    {
        const QList<int> ids = room.getDrawPile().mid(0, count);
        for (int id : ids) room.obtainCard(player, id, false);
        return ids;
    }

    CardMoveReason reason() const
    {
        return CardMoveReason(CardMoveReason::S_REASON_SWAP,
                              first->objectName(), QStringLiteral("movement-contract"), QString());
    }

    void clear()
    {
        packets.clear();
        probe->observations.clear();
        decodeFailed = false;
    }

    // Outlives the room, whose owned players supply the capture connections.
    QList<MovePacket> packets;
    bool decodeFailed = false;
    Room room;
    EngineRuntimeContextScope engineScope;
    LuaRuntime::Binding luaBinding;
    ServerPlayer *first;
    ServerPlayer *second;
    ServerPlayer *observer;
    MovementProbe *probe;
};

bool observerSeesCanonicalPair(const MovementFixture &fixture, Player::Place destination)
{
    int lost = 0, gained = 0;
    QVariant lostId, gainedId;
    MOVE_CHECK(!fixture.decodeFailed);
    for (const MovePacket &packet : fixture.packets) {
        if (packet.receiver != fixture.observer) continue;
        if (packet.command == S_COMMAND_LOSE_CARD) {
            ++lost;
            lostId = packet.payload.value("move_id");
        } else {
            ++gained;
            gainedId = packet.payload.value("move_id");
        }
        const QVariantList moves = packet.payload.value("moves").toList();
        MOVE_CHECK(!moves.isEmpty());
        for (const QVariant &value : moves) {
            const QVariantMap move = value.toMap();
            MOVE_CHECK(move.value("to_place").toInt() == int(destination));
            MOVE_CHECK(move.value("from_place").toInt() == int(Player::PlaceHand));
            MOVE_CHECK(!move.value("open").toBool());
            for (const QVariant &id : move.value("card_ids").toList())
                MOVE_CHECK(id.toInt() == Card::S_UNKNOWN_CARD_ID);
        }
    }
    MOVE_CHECK(lost == 1 && gained == 1);
    MOVE_CHECK(lostId.isValid() && lostId == gainedId);
    return true;
}

bool bottomMoveKeepsOrderAndEvents(bool redirect)
{
    MovementFixture fixture;
    const int initialPileSize = fixture.room.getDrawPile().size();
    const QList<int> ids = fixture.give(fixture.first, 2);
    MOVE_CHECK(ids.size() == 2);
    MOVE_CHECK(fixture.first->handCards() == ids);
    MOVE_CHECK(fixture.room.getDrawPile().size() == initialPileSize - ids.size());
    QList<int> expectedPile = fixture.room.getDrawPile();
    expectedPile << ids;
    fixture.clear();
    fixture.probe->redirectBottom = redirect;
    CardsMoveStruct move(ids, fixture.first, nullptr, Player::PlaceHand,
        redirect ? Player::DiscardPile : Player::DrawPileBottom, fixture.reason());
    fixture.room.moveCardsAtomic(move, false);
    MOVE_CHECK(fixture.room.getDrawPile() == expectedPile);
    MOVE_CHECK(fixture.room.getDrawPile().size() == initialPileSize);
    MOVE_CHECK(fixture.first->isKongcheng());
    for (int id : ids) {
        MOVE_CHECK(fixture.room.getCardOwner(id) == nullptr);
        MOVE_CHECK(fixture.room.getCardPlace(id) == Player::DrawPile);
    }
    MOVE_CHECK(fixture.probe->observations.size() == 2);
    const MoveObservation &completed = fixture.probe->observations.last();
    MOVE_CHECK(completed.event == CardsMoveOneTime);
    MOVE_CHECK(completed.move.to_place == Player::DrawPileBottom && completed.move.card_ids == ids);
    MOVE_CHECK(observerSeesCanonicalPair(fixture, Player::DrawPile));
    return true;
}

bool exchangeHasTwoStagesAndPrivateWire()
{
    MovementFixture fixture;
    const QList<int> firstIds = fixture.give(fixture.first, 2);
    const QList<int> secondIds = fixture.give(fixture.second, 1);
    MOVE_CHECK(firstIds.size() == 2 && secondIds.size() == 1);
    MOVE_CHECK(fixture.first->handCards() == firstIds);
    MOVE_CHECK(fixture.second->handCards() == secondIds);
    for (int id : firstIds) MOVE_CHECK(!secondIds.contains(id));
    fixture.clear();
    QList<CardsMoveStruct> moves;
    moves << CardsMoveStruct(firstIds, fixture.first, fixture.second,
        Player::PlaceHand, Player::PlaceHand, fixture.reason());
    moves << CardsMoveStruct(secondIds, fixture.second, fixture.first,
        Player::PlaceHand, Player::PlaceHand, fixture.reason());
    fixture.room.moveCards(moves, false, true);
    MOVE_CHECK(fixture.first->handCards() == secondIds);
    MOVE_CHECK(fixture.second->handCards() == firstIds);
    MOVE_CHECK(fixture.probe->observations.size() == 8);
    int losses = 0, gains = 0;
    for (const MoveObservation &observation : fixture.probe->observations) {
        const CardsMoveOneTimeStruct &move = observation.move;
        MOVE_CHECK(move.origin_from && move.origin_to);
        MOVE_CHECK(move.origin_to_place == Player::PlaceHand);
        MOVE_CHECK(move.origin_from_places.size() == move.card_ids.size());
        for (Player::Place place : move.origin_from_places) MOVE_CHECK(place == Player::PlaceHand);
        if (observation.event != CardsMoveOneTime) continue;
        if (move.to_place == Player::PlaceTable) {
            MOVE_CHECK(gains == 0 && observation.bothHandsEmpty);
            ++losses;
        } else {
            MOVE_CHECK(losses == 2 && move.to_place == Player::PlaceHand);
            for (Player::Place place : move.from_places) MOVE_CHECK(place == Player::PlaceTable);
            ++gains;
        }
    }
    MOVE_CHECK(losses == 2 && gains == 2);
    MOVE_CHECK(observerSeesCanonicalPair(fixture, Player::PlaceHand));
    return true;
}

bool exchangeRechecksDeadDestination(bool enforceOrigin)
{
    MovementFixture fixture;
    const QList<int> ids = fixture.give(fixture.first, 1);
    MOVE_CHECK(ids.size() == 1);
    fixture.probe->killOnLoss = fixture.second;
    fixture.clear();
    const CardsMoveStruct move(ids, fixture.first, fixture.second,
        Player::PlaceHand, Player::PlaceHand, fixture.reason());
    fixture.room.moveCards(QList<CardsMoveStruct>() << move, false, enforceOrigin);
    MOVE_CHECK(fixture.second->isDead());
    if (enforceOrigin) {
        MOVE_CHECK(fixture.room.getCardPlace(ids.first()) == Player::DiscardPile);
        MOVE_CHECK(fixture.room.getCardOwner(ids.first()) == nullptr);
        MOVE_CHECK(fixture.room.getDiscardPile().contains(ids.first()));
    } else {
        MOVE_CHECK(fixture.room.getCardPlace(ids.first()) == Player::PlaceHand);
        MOVE_CHECK(fixture.room.getCardOwner(ids.first()) == fixture.second);
    }
    return true;
}

bool interruptedExchangeClosesMovementPair()
{
    MovementFixture fixture;
    const QList<int> ids = fixture.give(fixture.first, 1);
    MOVE_CHECK(ids.size() == 1);
    fixture.clear();
    fixture.probe->interruptLoss = true;
    bool interrupted = false;
    try {
        const CardsMoveStruct move(ids, fixture.first, fixture.second,
            Player::PlaceHand, Player::PlaceHand, fixture.reason());
        fixture.room.moveCards(QList<CardsMoveStruct>() << move, false, true);
    } catch (TriggerEvent event) {
        interrupted = event == TurnBroken;
    }
    MOVE_CHECK(interrupted && fixture.room.getCardPlace(ids.first()) == Player::PlaceTable);
    int lost = 0, gained = 0;
    QVariant lostId, gainedId;
    for (const MovePacket &packet : fixture.packets) {
        if (packet.receiver != fixture.observer) continue;
        if (packet.command == S_COMMAND_LOSE_CARD) {
            ++lost;
            lostId = packet.payload.value("move_id");
        } else {
            ++gained;
            gainedId = packet.payload.value("move_id");
            for (const QVariant &value : packet.payload.value("moves").toList()) {
                const QVariantMap move = value.toMap();
                MOVE_CHECK(move.value("to_place").toInt() == int(Player::PlaceTable));
                for (const QVariant &id : move.value("card_ids").toList())
                    MOVE_CHECK(id.toInt() == Card::S_UNKNOWN_CARD_ID);
            }
        }
    }
    MOVE_CHECK(!fixture.decodeFailed && lost == 1 && gained == 1);
    MOVE_CHECK(lostId.isValid() && lostId == gainedId);
    return true;
}

#undef MOVE_CHECK

}

int runOriginalHegemonyMovementTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "Card movement test engine initialization failed:" << error;
        return 1;
    }
    const int delay = Config.AIDelay;
    auto restore = qScopeGuard([=]() { Config.AIDelay = delay; });
    Config.AIDelay = 0;
    if (!bottomMoveKeepsOrderAndEvents(false)) return 2;
    if (!bottomMoveKeepsOrderAndEvents(true)) return 3;
    if (!exchangeHasTwoStagesAndPrivateWire()) return 4;
    if (!exchangeRechecksDeadDestination(true)) return 5;
    if (!exchangeRechecksDeadDestination(false)) return 6;
    if (!interruptedExchangeClosesMovementPair()) return 7;
    qInfo() << "ORIGINAL_HEGEMONY_MOVEMENT_TEST_RESULT status=PASS";
    return 0;
}

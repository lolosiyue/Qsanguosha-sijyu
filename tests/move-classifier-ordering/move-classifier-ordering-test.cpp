#include "card-movement-service.h"
#include "engine-bootstrap.h"
#include "room.h"

#include <QCoreApplication>
#include <QMap>
#include <QTextStream>

#include <algorithm>
#include <functional>
#include <iterator>

struct CardMovementServiceTestAccess
{
    using MoveSourceClassifier = CardMovementService::_MoveSourceClassifier;
    using MoveMergeClassifier = CardMovementService::_MoveMergeClassifier;
    using MoveSeparateClassifier = CardMovementService::_MoveSeparateClassifier;
};

namespace {
template <typename T>
bool verifyStrictWeakOrdering(const QString &label, const QList<T> &values)
{
    bool ok = true;

    for (int i = 0; i < values.size(); ++i) {
        if (values[i] < values[i]) {
            QTextStream(stderr) << label << ": irreflexive check failed at " << i << "\n";
            ok = false;
        }

        for (int j = 0; j < values.size(); ++j) {
            if (values[i] < values[j] && values[j] < values[i]) {
                QTextStream(stderr) << label << ": asymmetry check failed at "
                                    << i << ", " << j << "\n";
                ok = false;
            }

            const bool equivalent = !(values[i] < values[j]) && !(values[j] < values[i]);
            if (equivalent != (values[i] == values[j])) {
                QTextStream(stderr) << label << ": equality consistency failed at "
                                    << i << ", " << j << "\n";
                ok = false;
            }

            for (int k = 0; k < values.size(); ++k) {
                if (values[i] < values[j] && values[j] < values[k]
                        && !(values[i] < values[k])) {
                    QTextStream(stderr) << label << ": transitivity check failed at "
                                        << i << ", " << j << ", " << k << "\n";
                    ok = false;
                }
            }
        }
    }

    QMap<T, int> map;
    for (int i = 0; i < values.size(); ++i)
        map.insert(values[i], i);
    if (map.size() != values.size()) {
        QTextStream(stderr) << label << ": QMap retained " << map.size()
                            << " of " << values.size() << " distinct keys\n";
        ok = false;
    }

    return ok;
}

CardsMoveStruct makeMove(Player *from, Player *to, Player::Place fromPlace,
                         Player::Place toPlace, const QString &fromPile,
                         const QString &toPile, const CardMoveReason &reason)
{
    CardsMoveStruct move;
    move.from = from;
    move.to = to;
    move.from_place = fromPlace;
    move.to_place = toPlace;
    move.from_pile_name = fromPile;
    move.to_pile_name = toPile;
    move.reason = reason;
    return move;
}

CardsMoveOneTimeStruct makeOneTimeMove(const CardsMoveStruct &move)
{
    CardsMoveOneTimeStruct oneTime;
    oneTime.card_ids << 1;
    oneTime.from = move.from;
    oneTime.to = move.to;
    oneTime.from_places << move.from_place;
    oneTime.to_place = move.to_place;
    oneTime.from_pile_names << move.from_pile_name;
    oneTime.to_pile_name = move.to_pile_name;
    oneTime.open << false;
    oneTime.reason = move.reason;
    oneTime.is_last_handcard = false;
    return oneTime;
}

bool verifyLocationIndex(ServerPlayer *owner)
{
    CardLocationIndex index;
    if (index.owner(10) != nullptr || index.place(10) != Player::PlaceTable)
        return false;
    if (index.place(-1) != Player::PlaceUnknown)
        return false;

    index.set(10, owner, Player::PlaceHand);
    return index.owner(10) == owner && index.place(10) == Player::PlaceHand;
}

bool verifyNormalizeFacade()
{
    Room room(nullptr, QStringLiteral("02_1v1"));
    const QList<int> pile = room.getDrawPile();
    if (pile.length() < 2)
        return false;

    const int firstId = pile.at(0);
    const int secondId = pile.at(1);
    room.setCardMapping(firstId, nullptr, Player::DrawPile);
    room.setCardMapping(secondId, nullptr, Player::DrawPile);

    CardsMoveStruct move;
    move.card_ids << firstId << firstId << secondId;
    move.to_place = Player::DiscardPile;
    move.reason = CardMoveReason(CardMoveReason::S_REASON_PUT, "normalize-test");

    CardsMoveStruct duplicateMove = move;
    duplicateMove.card_ids = QList<int>() << secondId;
    const QList<CardsMoveStruct> normalized = room._breakDownCardMoves(
        QList<CardsMoveStruct>() << move << duplicateMove);
    if (normalized.length() != 1
        || normalized.first().card_ids != (QList<int>() << firstId << secondId))
        return false;

    CardsMoveStruct noOpMove;
    noOpMove.card_ids << firstId;
    noOpMove.to_place = Player::DrawPile;
    return room._breakDownCardMoves(QList<CardsMoveStruct>() << noOpMove).isEmpty();
}
}

int runCardMovementServiceTests()
{
    QString engineError;
    if (!EngineBootstrap::initialize(false, &engineError)) {
        QTextStream(stderr) << "engine initialization failed: " << engineError << "\n";
        return 1;
    }
    bool ok = true;

    alignas(void *) unsigned char playerStorageA[1];
    alignas(void *) unsigned char playerStorageB[1];
    alignas(void *) unsigned char playerStorageC[1];
    Player *players[] = {
        reinterpret_cast<Player *>(playerStorageA),
        reinterpret_cast<Player *>(playerStorageB),
        reinterpret_cast<Player *>(playerStorageC)
    };
    const std::less<Player *> playerLess;
    std::sort(std::begin(players), std::end(players), playerLess);

    const CardMoveReason reasonA(CardMoveReason::S_REASON_USE, "player_b", "skill_a", "event_b");
    const CardMoveReason reasonB(CardMoveReason::S_REASON_RESPONSE, "player_a", "skill_b", "event_a");
    const CardMoveReason reasonC(CardMoveReason::S_REASON_DISCARD, "player_c", "skill_c", "event_c");
    const QList<CardMoveReason> reasons = { reasonA, reasonB, reasonC };
    ok = verifyStrictWeakOrdering("CardMoveReason", reasons) && ok;

    const CardsMoveStruct moveA = makeMove(players[0], players[2], Player::PlaceEquip,
                                           Player::DiscardPile, "pile_z", "to_a", reasonB);
    const CardsMoveStruct moveB = makeMove(players[1], players[1], Player::PlaceHand,
                                           Player::PlaceEquip, "pile_a", "to_z", reasonA);
    const CardsMoveStruct moveC = makeMove(players[2], players[0], Player::PlaceSpecial,
                                           Player::PlaceHand, "pile_m", "to_m", reasonC);
    const QList<CardsMoveStruct> moves = { moveA, moveB, moveC };
    ok = verifyStrictWeakOrdering("CardsMoveStruct", moves) && ok;

    using Access = CardMovementServiceTestAccess;
    const QList<Access::MoveSourceClassifier> sourceKeys = {
        Access::MoveSourceClassifier(moveA),
        Access::MoveSourceClassifier(moveB),
        Access::MoveSourceClassifier(moveC)
    };
    ok = verifyStrictWeakOrdering("MoveSourceClassifier", sourceKeys) && ok;

    const QList<Access::MoveMergeClassifier> mergeKeys = {
        Access::MoveMergeClassifier(moveA),
        Access::MoveMergeClassifier(moveB),
        Access::MoveMergeClassifier(moveC)
    };
    ok = verifyStrictWeakOrdering("MoveMergeClassifier", mergeKeys) && ok;

    const QList<Access::MoveSeparateClassifier> separateKeys = {
        Access::MoveSeparateClassifier(makeOneTimeMove(moveA), 0),
        Access::MoveSeparateClassifier(makeOneTimeMove(moveB), 0),
        Access::MoveSeparateClassifier(makeOneTimeMove(moveC), 0)
    };
    ok = verifyStrictWeakOrdering("MoveSeparateClassifier", separateKeys) && ok;
    ok = verifyLocationIndex(reinterpret_cast<ServerPlayer *>(players[0])) && ok;
    ok = verifyNormalizeFacade() && ok;

    if (ok)
        QTextStream(stdout) << "card movement service tests passed\n";
    return ok ? 0 : 1;
}

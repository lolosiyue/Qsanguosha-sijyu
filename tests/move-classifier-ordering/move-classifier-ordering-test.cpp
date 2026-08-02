#include "skill.h"
#include "skill-instance-utils.h"
#include "skill-instance-attachment-registry.h"
#include "skill-execution-registry.h"
#include "protocol.h"
#include "room-state.h"
#include "json.h"

// 測試私有分類器本身，避免只複製比較邏輯而產生無效測試。
#define private public
#include "room.h"
#undef private

#include <QCoreApplication>
#include <QMap>
#include <QTextStream>

#include <algorithm>
#include <functional>
#include <iterator>

// CardMoveReason 內含 CardUseStruct；測試不連結完整 RoomThread，僅提供同等預設狀態。
CardUseStruct::CardUseStruct()
    : card(nullptr), from(nullptr), m_isOwnerUse(true), m_addHistory(true),
      m_isHandcard(false), m_validateTargets(false), whocard(nullptr), who(nullptr),
      extra_use(0), bypass_cost(false), skipSkillEffect(false),
      hasSkillActivationRequest(false), skillExecutionID(0)
{
}

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
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
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

    const QList<Room::_MoveSourceClassifier> sourceKeys = {
        Room::_MoveSourceClassifier(moveA),
        Room::_MoveSourceClassifier(moveB),
        Room::_MoveSourceClassifier(moveC)
    };
    ok = verifyStrictWeakOrdering("_MoveSourceClassifier", sourceKeys) && ok;

    const QList<Room::_MoveMergeClassifier> mergeKeys = {
        Room::_MoveMergeClassifier(moveA),
        Room::_MoveMergeClassifier(moveB),
        Room::_MoveMergeClassifier(moveC)
    };
    ok = verifyStrictWeakOrdering("_MoveMergeClassifier", mergeKeys) && ok;

    const QList<Room::_MoveSeparateClassifier> separateKeys = {
        Room::_MoveSeparateClassifier(makeOneTimeMove(moveA), 0),
        Room::_MoveSeparateClassifier(makeOneTimeMove(moveB), 0),
        Room::_MoveSeparateClassifier(makeOneTimeMove(moveC), 0)
    };
    ok = verifyStrictWeakOrdering("_MoveSeparateClassifier", separateKeys) && ok;

    if (ok)
        QTextStream(stdout) << "move classifier ordering tests passed\n";
    return ok ? 0 : 1;
}

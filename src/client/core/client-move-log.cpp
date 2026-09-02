#include "client-move-log.h"

#include "protocol.h"

namespace {

constexpr int PlaceHand = 0;
constexpr int PlaceEquip = 1;
constexpr int PlaceDelayedTrick = 2;
constexpr int PlaceJudge = 3;
constexpr int PlaceSpecial = 4;
constexpr int DiscardPile = 5;
constexpr int DrawPile = 6;
constexpr int PlaceTable = 7;
constexpr int UnknownCardId = -1;

constexpr int ReasonPut = 0x0A;
constexpr int ReasonExclusive = 0x68;
constexpr int ReasonTurnover = 0x18;
constexpr int ReasonPreview = 0x38;
constexpr int ReasonShuffle = 0x5A;
constexpr int ReasonPutEnd = 0x6A;
constexpr int ReasonTransfer = 0x09;

QList<int> cardIdsOf(const QVariantMap &move)
{
    QList<int> ids;
    const QVariant value = move.value(QStringLiteral("card_ids"));
    if (value.userType() == QMetaType::QStringList) {
        for (const QString &token : value.toStringList())
            ids.append(token.toInt());
        return ids;
    }
    for (const QVariant &entry : value.toList())
        ids.append(entry.toInt());
    return ids;
}

QString joinCardIds(const QList<int> &ids)
{
    QStringList tokens;
    for (int id : ids)
        tokens.append(QString::number(id));
    return tokens.join(QLatin1Char('+'));
}

bool hasUnknown(const QList<int> &ids)
{
    return ids.contains(UnknownCardId);
}

int placeOf(const QVariantMap &move, const QString &field)
{
    return move.value(field).toInt();
}

QString playerOf(const QVariantMap &move, const QString &field)
{
    return move.value(field).toString();
}

QVariantMap reasonOf(const QVariantMap &move)
{
    return move.value(QStringLiteral("reason")).toMap();
}

int reasonCode(const QVariantMap &move)
{
    return reasonOf(move).value(QStringLiteral("reason")).toInt();
}

bool shouldIgnoreDisplayMove(const QVariantMap &move)
{
    if (move.value(QStringLiteral("to_pile")).toString().startsWith(QLatin1Char('#'))
        || move.value(QStringLiteral("from_pile")).toString().startsWith(QLatin1Char('#'))) {
        return true;
    }
    if (placeOf(move, QStringLiteral("to_place")) == DiscardPile) {
        const int fromPlace = placeOf(move, QStringLiteral("from_place"));
        return fromPlace == PlaceTable || fromPlace == PlaceJudge;
    }
    return false;
}

ClientLogRecord record(const QString &type, const QString &from, const QStringList &tos,
                       const QString &cardString, const QString &arg = QString(),
                       const QString &arg2 = QString())
{
    ClientLogRecord value;
    value.type = type;
    value.from = from;
    value.tos = tos;
    value.cardString = cardString;
    value.arg = arg;
    value.arg2 = arg2;
    return value;
}

} // namespace

QVariantMap ClientLogRecord::toSkillLogMap() const
{
    return QVariantMap{
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("log_type"), type},
        {QStringLiteral("from_player"), from},
        {QStringLiteral("to_players"), tos},
        {QStringLiteral("card_string"), cardString},
        {QStringLiteral("arguments"), QStringList{arg, arg2, arg3, arg4, arg5}}};
}

QList<ClientLogRecord> synthesizeLoseCardLogs(const QVariantMap &move)
{
    if (shouldIgnoreDisplayMove(move))
        return {};
    if (placeOf(move, QStringLiteral("from_place")) != PlaceEquip)
        return {};
    return {record(QStringLiteral("#Uninstall"), playerOf(move, QStringLiteral("from_player")),
                   {}, joinCardIds(cardIdsOf(move)))};
}

QList<ClientLogRecord> synthesizeGetCardLogs(const QVariantMap &move)
{
    if (shouldIgnoreDisplayMove(move))
        return {};

    QList<ClientLogRecord> logs;
    const QList<int> ids = cardIdsOf(move);
    const QString cardString = joinCardIds(ids);
    const QString count = QString::number(ids.size());
    const QString fromPlayer = playerOf(move, QStringLiteral("from_player"));
    const QString toPlayer = playerOf(move, QStringLiteral("to_player"));
    const int fromPlace = placeOf(move, QStringLiteral("from_place"));
    const int toPlace = placeOf(move, QStringLiteral("to_place"));
    const int reason = reasonCode(move);
    const bool unknown = hasUnknown(ids);
    const bool hasFrom = !fromPlayer.isEmpty();

    if (toPlace == PlaceHand) {
        if (fromPlace == DrawPile) {
            logs.append(record(QStringLiteral("$DrawCards"), toPlayer, {}, cardString, count));
        } else if (fromPlace == DiscardPile) {
            logs.append(record(QStringLiteral("$RecycleCard"), toPlayer, {}, cardString));
        } else if (fromPlace == PlaceSpecial) {
            logs.append(record(QStringLiteral("#GotNCardFromPile"), toPlayer, {fromPlayer},
                               cardString, move.value(QStringLiteral("from_pile")).toString(),
                               count));
        } else if (fromPlace == PlaceTable || fromPlace == PlaceJudge) {
            if (reason != ReasonPreview && !unknown) {
                logs.append(record(reason == ReasonExclusive ? QStringLiteral("$TakeAG")
                                                             : QStringLiteral("$GotCardBack"),
                                   toPlayer, {}, cardString));
            }
        } else if (hasFrom) {
            if (fromPlayer == toPlayer && fromPlace == PlaceEquip) {
                logs.append(record(QStringLiteral("$GotCardBack"), toPlayer, {}, cardString));
            } else if (unknown) {
                logs.append(record(QStringLiteral("#MoveNCards"), fromPlayer, {toPlayer},
                                   QString(), count));
            } else {
                logs.append(record(QStringLiteral("$MoveCard"), fromPlayer, {toPlayer},
                                   cardString));
            }
        }
    } else if (toPlace == PlaceSpecial) {
        const QString pile = move.value(QStringLiteral("to_pile")).toString();
        if (!pile.startsWith(QLatin1Char('#'))) {
            if (unknown) {
                logs.append(record(QStringLiteral("#RemoveFromGame"), toPlayer, {}, QString(),
                                   pile, count));
            } else {
                logs.append(record(QStringLiteral("$AddToPile"), toPlayer, {}, cardString, pile));
            }
        }
    } else if (hasFrom) {
        if (toPlace == PlaceDelayedTrick) {
            QString type = QStringLiteral("$LightningMove");
            if (fromPlace != PlaceDelayedTrick && reason != ReasonTransfer)
                type = QStringLiteral("$PasteCard");
            if (unknown)
                type = QStringLiteral("#LightningMove");
            logs.append(record(type, fromPlayer, {toPlayer}, cardString, count));
        } else if (toPlace == DrawPile) {
            if (reason == ReasonPut
                && reasonOf(move).value(QStringLiteral("skill_name")).toString()
                    == QLatin1String("luck_card")) {
                return logs;
            }
            QString type = QStringLiteral("$PutCard");
            if (reason == ReasonShuffle)
                type = QStringLiteral("$ShuffleCard");
            else if (reason == ReasonPutEnd)
                type = QStringLiteral("$PutCardEnd");
            logs.append(record(type, fromPlayer, {}, cardString, count));
        }
    }

    if (toPlace == PlaceEquip) {
        if (hasFrom && fromPlayer != toPlayer) {
            if (unknown) {
                logs.append(record(QStringLiteral("#MoveNCards"), fromPlayer, {toPlayer},
                                   QString(), count));
            } else {
                logs.append(record(QStringLiteral("$MoveCard"), fromPlayer, {toPlayer},
                                   cardString));
            }
        }
        logs.append(record(QStringLiteral("#Install"), toPlayer, {}, cardString));
    }
    if (reason == ReasonTurnover) {
        logs.append(record(QStringLiteral("$TurnOver"),
                           reasonOf(move).value(QStringLiteral("player_id")).toString(), {},
                           cardString));
    }
    return logs;
}

static QList<ClientLogRecord> synthesizeRenPileLogs(int command, const QVariantMap &move,
                                             QList<int> *renPile)
{
    if (renPile == nullptr)
        return {};
    QList<ClientLogRecord> logs;
    if (command == QSanProtocol::S_COMMAND_LOSE_CARD) {
        if (placeOf(move, QStringLiteral("from_place")) != PlaceTable || renPile->isEmpty())
            return {};
        QList<int> ids;
        for (int id : cardIdsOf(move)) {
            if (renPile->contains(id)) {
                renPile->removeAll(id);
                ids.append(id);
            }
        }
        if (!ids.isEmpty()) {
            logs.append(record(QStringLiteral("$removeRenPile"), QString(), {},
                               joinCardIds(ids), QString::number(ids.size()),
                               QStringLiteral("ren_pile")));
        }
    } else if (command == QSanProtocol::S_COMMAND_GET_CARD) {
        if (placeOf(move, QStringLiteral("to_place")) == PlaceTable
            && move.value(QStringLiteral("to_pile")).toString()
                == QLatin1String("ren_pile")) {
            const QList<int> ids = cardIdsOf(move);
            *renPile += ids;
            logs.append(record(QStringLiteral("$addRenPile"),
                               reasonOf(move).value(QStringLiteral("player_id")).toString(), {},
                               joinCardIds(ids), QString::number(ids.size()),
                               QStringLiteral("ren_pile")));
        }
    }
    return logs;
}

QList<ClientLogRecord> synthesizeCardMovementLogs(int command, const QVariantMap &payload,
                                                  QList<int> *renPile)
{
    QList<ClientLogRecord> logs;
    const QVariantList moves = payload.value(QStringLiteral("moves")).toList();
    for (const QVariant &entry : moves) {
        const QVariantMap move = entry.toMap();
        logs.append(synthesizeRenPileLogs(command, move, renPile));
        if (command == QSanProtocol::S_COMMAND_LOSE_CARD)
            logs.append(synthesizeLoseCardLogs(move));
        else if (command == QSanProtocol::S_COMMAND_GET_CARD)
            logs.append(synthesizeGetCardLogs(move));
    }
    return logs;
}

QList<ClientLogRecord> synthesizeHpChangeLogs(const QVariantMap &payload, int hpAfter, int maxHp)
{
    QList<ClientLogRecord> logs;
    const QString who = payload.value(QStringLiteral("player_name")).toString();
    const int delta = payload.value(QStringLiteral("delta")).toInt();
    const int nature = payload.value(QStringLiteral("nature")).toInt();
    const int lostHj = payload.value(QStringLiteral("lost_hp")).toInt();
    if (delta <= 0) {
        if (nature < 0)
            logs.append(record(QStringLiteral("#LoseHp"), who, {}, QString(),
                               QString::number(-delta)));
    } else {
        logs.append(record(QStringLiteral("#Recover"), who, {}, QString(),
                           QString::number(delta)));
    }
    logs.append(record(QStringLiteral("#GetHp"), who, {}, QString(),
                       QString::number(hpAfter + lostHj), QString::number(maxHp)));
    return logs;
}

QList<ClientLogRecord> synthesizeMaxHpChangeLogs(const QString &who, int hp, int maxHpAfter)
{
    return {record(QStringLiteral("#GetHp"), who, {}, QString(), QString::number(hp),
                   QString::number(maxHpAfter))};
}

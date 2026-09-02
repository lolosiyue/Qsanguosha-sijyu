#ifndef CLIENT_MOVE_LOG_H
#define CLIENT_MOVE_LOG_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// One ClientLogBox appendLog() call, without HTML. RoomScene synthesises
// these from GET_CARD / LOSE_CARD / CHANGE_HP; the server does not sendLog them.
struct ClientLogRecord
{
    QString type;
    QString from;
    QStringList tos;
    QString cardString;
    QString arg;
    QString arg2;
    QString arg3;
    QString arg4;
    QString arg5;

    QVariantMap toSkillLogMap() const;
};

// Place integers match Player::Place. Reason integers match CardMoveReason.
// renPile tracks table cards currently in 仁区, matching RoomScene::RenPile.
QList<ClientLogRecord> synthesizeLoseCardLogs(const QVariantMap &move);
QList<ClientLogRecord> synthesizeGetCardLogs(const QVariantMap &move);
QList<ClientLogRecord> synthesizeCardMovementLogs(int command, const QVariantMap &payload,
                                                  QList<int> *renPile = nullptr);
QList<ClientLogRecord> synthesizeHpChangeLogs(const QVariantMap &payload, int hpAfter,
                                              int maxHp);
QList<ClientLogRecord> synthesizeMaxHpChangeLogs(const QString &who, int hp, int maxHpAfter);

#endif

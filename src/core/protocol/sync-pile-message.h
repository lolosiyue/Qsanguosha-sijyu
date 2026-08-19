#ifndef SYNC_PILE_MESSAGE_H
#define SYNC_PILE_MESSAGE_H

#include <QList>
#include <QString>
#include <QVariant>

struct SyncPileMessage
{
    QString playerName;
    QString pileName;
    QList<int> cardIds;

    QVariant toVariant() const;
    bool tryParse(const QVariant &value);
};

#endif

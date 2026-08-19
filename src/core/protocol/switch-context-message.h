#ifndef SWITCH_CONTEXT_MESSAGE_H
#define SWITCH_CONTEXT_MESSAGE_H

#include <QString>
#include <QVariant>

struct SwitchContextMessage
{
    QString playerName;

    QVariant toVariant() const;
    bool tryParse(const QVariant &value);
};

#endif

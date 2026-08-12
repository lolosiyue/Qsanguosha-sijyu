#ifndef CARD_PROVENANCE_MESSAGE_H
#define CARD_PROVENANCE_MESSAGE_H

#include <QString>
#include <QVariant>

struct CardProvenanceMessage
{
    static constexpr int CurrentVersion = 2;

    int version = CurrentVersion;
    QString kind;
    QString initiator;
    QString card;
    QString sourceOwner;
    QString sourceSkill;
    int sourceInstanceId = 0;
    QString activationOwner;
    QString activationSkill;
    int activationInstanceId = 0;

    QVariant toVariant() const;
    bool tryParse(const QVariant &value);
};

#endif

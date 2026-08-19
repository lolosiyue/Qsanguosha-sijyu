#ifndef PLAYER_UI_STATE_H
#define PLAYER_UI_STATE_H

#include <QStringList>
#include <QVariant>

struct PlayerUIState
{
    int handMax = 0;
    int offensiveDistance = 0;
    int defensiveDistance = 0;

    QStringList maxCardsSkills;
    QStringList offensiveSkills;
    QStringList defensiveSkills;
    QStringList viewAsEquipSkills;

    QVariant toVariant() const;
    bool tryParse(const QVariant &value);

    bool operator==(const PlayerUIState &other) const;
};

struct PlayerUIStateMessage
{
    QString playerName;
    PlayerUIState state;

    QVariant toVariant() const;
    bool tryParse(const QVariant &value);
};

#endif

#ifndef GAME_ACTION_MODEL_H
#define GAME_ACTION_MODEL_H

#include "interaction-model.h"

#include <QJsonObject>
#include <QList>
#include <QString>

struct GameActionEntry
{
    QString id; // Stable submitted value or view-model identifier; never derived from label.
    QString label;
    bool enabled = true;
    bool selected = false;
    QString reason;
    int selectedVotes = 0;
    int maxVotes = 0;

    QJsonObject toJson() const;
};

// UI-owned selection draft. Submission must still pass through ClientCore validation.
struct GameActionModel
{
    quint64 sessionGeneration = 0;
    quint64 presentationRevision = 0;
    quint64 requestId = 0;
    InteractionRequest request;
    bool supported = false;
    QString unsupportedReason;
    QString prompt;
    QString actionContext; // Distinguishes request choices from a skill's local options.
    QList<GameActionEntry> actions;
    QList<GameActionEntry> cards;
    QList<GameActionEntry> players;
    QList<GameActionEntry> skills;
    // Ordered views of the existing rearrangement draft; not a second card store.
    QList<GameActionEntry> topCards;
    QList<GameActionEntry> bottomCards;
    bool arrangingCards = false;
    bool canMoveToTop = false;
    bool canMoveToBottom = false;
    int minSelection = 0;
    int maxSelection = 0;
    bool canConfirm = false;
    bool canCancel = false;
    bool canFinish = false;

    bool isCurrentFor(quint64 generation, quint64 revision, quint64 request) const;
    QJsonObject toJson() const;
};

Q_DECLARE_METATYPE(GameActionModel)

#endif

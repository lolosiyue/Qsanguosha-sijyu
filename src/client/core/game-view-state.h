#ifndef GAME_VIEW_STATE_H
#define GAME_VIEW_STATE_H

#include "client-game-state.h"
#include "interaction-model.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVariantMap>
#include <functional>

struct GameViewFormatOptions
{
    QString operatingPlayer;
    QStringList authorizedHandPlayers;
    bool stateReady = true;
    std::function<QString(const QString &)> translate;
    std::function<QString(int)> cardLabel;
    std::function<QString(const QString &)> playerLabel;
    std::function<QString(const QString &)> phaseLabel;
    // Return an empty string when the current rule projection has no distance.
    std::function<QString(const QString &, const QString &)> distanceLabel;
};

struct GameViewCard
{
    int id = -1;
    QString label;
    QString place;
    bool visible = false;

    QJsonObject toJson() const;
};

struct GameViewPlayer
{
    QString name;
    QString label;
    int seat = -1;
    int hp = 0;
    int maxHp = 0;
    int handCount = 0;
    QString distanceFromOperatingPlayer;
    bool alive = true;
    QVariant faceUp;
    QVariant chained;
    QVariant removed;
    QString role;
    QString kingdom;
    bool self = false;
    bool handVisible = false;
    // Public/recipient-projected dashboard details for the lightweight inspector.
    int handMax = -1;
    int offensiveDistance = -1;
    int defensiveDistance = -1;
    QVariantMap marks;
    QStringList skills;
    QList<GameViewCard> hand;
    QList<GameViewCard> equipment;
    QList<GameViewCard> judging;
    QVariantMap piles;
    QString general;

    QJsonObject toJson() const;
};

// Immutable-by-convention, QtCore-only projection of the current client state.
struct GameViewState
{
    quint64 sessionGeneration = 0;
    quint64 presentationRevision = 0;
    quint64 requestId = 0;
    bool ready = true;
    QString selfName;
    QString selfLabel;
    QString currentPlayer;
    QString currentPlayerLabel;
    QString operatingPlayer;
    QString operatingPlayerLabel;
    QString phase;
    QString phaseLabel;
    QString prompt;
    int drawPileCount = -1;
    int discardPileCount = 0;
    bool playOrderReversed = false;
    QList<GameViewPlayer> players;
    QVariantMap privatePiles;
    QVariantList recentEvents;
    // docs/ui-roadmap.md 2.7 asks who is acting on whom.  The battle log already
    // carries that as structured fields, so it is projected as relations rather
    // than re-derived from the narration in recentEvents.
    QVariantList recentRelations;
    bool playOrderKnown = false;
    QStringList responseFocus;
    quint64 responseFocusRevision = 0;
    QVariantMap responseCountdown;
    QString focusResolutionId;
    bool resolutionAvailable = false;
    QVariantList activeResolutions;

    static GameViewState fromState(const ClientGameState &state,
                                   const InteractionRequest *request = nullptr,
                                   quint64 generation = 0, quint64 revision = 0,
                                   const GameViewFormatOptions &options = {});
    QJsonObject toJson() const;
    QString toPlainText() const;
};

Q_DECLARE_METATYPE(GameViewState)

#endif

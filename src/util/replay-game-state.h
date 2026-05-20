#ifndef _REPLAY_GAME_STATE_H
#define _REPLAY_GAME_STATE_H

#include <QObject>
#include <QMap>
#include <QList>
#include <QString>
#include <QVariant>

class GameSnapshot;
struct GlobalSnapshot;
struct PlayerSnapshot;

class ReplayGameState : public QObject
{
    Q_OBJECT

public:
    explicit ReplayGameState(QObject *parent = nullptr);

    bool rebuildFromCommands(const QList<QPair<int, QString>> &pairs, int upToIndex);
    bool applyCommand(const QString &cmd);
    bool applySnapshot(GameSnapshot *snapshot);

    PlayerSnapshot* getPlayerState(const QString &playerName);
    GlobalSnapshot getGlobalState() const;

    int getCardPosition(int cardId) const;
    QString getCardOwner(int cardId) const;
    QString getCardPile(int cardId) const;

    bool validateState() const;
    void clear();

    int getTurnCount() const;
    QString getCurrentPlayer() const;

private:
    bool processSetup(const QVariant &body);
    bool processAddPlayer(const QVariant &body);
    bool processRemovePlayer(const QVariant &body);
    bool processSetProperty(const QVariant &body);
    bool processSetMark(const QVariant &body);
    bool processMoveCards(const QVariant &body);
    bool processChangeHp(const QVariant &body);
    bool processGameOver(const QVariant &body);
    bool processLogSkill(const QVariant &body);

    void updateCardMapping(int cardId, const QString &owner, const QString &pile);

    GlobalSnapshot m_state;
    QMap<int, QString> m_cardOwnerMap;
    QMap<int, QString> m_cardPileMap;
    QMap<QString, PlayerSnapshot*> m_playerMap;
};

#endif
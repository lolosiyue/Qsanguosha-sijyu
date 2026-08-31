#ifndef CLIENT_GAME_STATE_H
#define CLIENT_GAME_STATE_H

#include <QJsonObject>
#include <QMap>
#include <QStringList>
#include <QVariantMap>

class ClientGameState
{
public:
    void reset();
    void resetGameplayState();

    void setConnectionValue(const QString &key, const QVariant &value);
    QVariant connectionValue(const QString &key) const;
    QVariantMap connection() const { return m_connection; }
    void setSetup(const QVariantMap &setup);
    QVariantMap setup() const { return m_setup; }
    void setGameValue(const QString &key, const QVariant &value);
    QVariant gameValue(const QString &key) const;
    QVariantMap game() const { return m_game; }

    void setSelfName(const QString &name);
    QString selfName() const { return m_selfName; }
    void setPlayerNames(const QStringList &names);
    QStringList playerNames() const { return m_playerNames; }
    void addPlayer(const QString &name);
    void removePlayer(const QString &name);
    bool hasPlayer(const QString &name) const;
    void setPlayerValue(const QString &name, const QString &key, const QVariant &value);
    QVariant playerValue(const QString &name, const QString &key) const;
    QVariantMap player(const QString &name) const;
    void setPlayerMark(const QString &name, const QString &mark, int value);
    void setPlayerAlive(const QString &name, bool alive);
    bool isPlayerAlive(const QString &name) const;

    void setCardIdSpace(int count);
    int cardIdSpace() const { return m_cardIdSpace; }
    bool isKnownCardId(int cardId) const;
    void setCardValue(int cardId, const QString &key, const QVariant &value);
    QVariantMap card(int cardId) const;
    QList<int> cardsForPlayer(const QString &playerName, int place = -1) const;

    void recordFlow(int command, const QVariant &payload);
    QVariant latestPayload(int command) const;
    int flowCount(int command) const;
    void appendPresentationEvent(int command, const QString &text,
                                 const QVariant &payload = QVariant());
    QVariantList presentationEvents() const { return m_presentationEvents; }

    QJsonObject toJson() const;

private:
    QVariantMap &ensurePlayer(const QString &name);
    QVariantMap &ensureCard(int cardId);

    QVariantMap m_connection;
    QVariantMap m_setup;
    QVariantMap m_game;
    QString m_selfName;
    QStringList m_playerNames;
    QMap<QString, QVariantMap> m_players;
    QMap<int, QVariantMap> m_cards;
    QMap<int, QVariant> m_latestPayloads;
    QMap<int, int> m_flowCounts;
    QVariantList m_presentationEvents;
    int m_cardIdSpace = 0;
};

#endif

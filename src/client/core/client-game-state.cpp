#include "client-game-state.h"

#include <QJsonArray>

void ClientGameState::reset()
{
    m_selfName.clear();
    m_playerNames.clear();
    m_alive.clear();
    m_cardIdSpace = 0;
}

void ClientGameState::setSelfName(const QString &name)
{
    m_selfName = name;
}

void ClientGameState::setPlayerNames(const QStringList &names)
{
    m_playerNames = names;
    // 唔喺清單入面嘅生死資料冇意義,清走,避免 rejoin 之後讀返舊值。
    QHash<QString, bool> alive;
    foreach (const QString &name, names) {
        if (m_alive.contains(name))
            alive.insert(name, m_alive.value(name));
    }
    m_alive = alive;
}

void ClientGameState::addPlayer(const QString &name)
{
    if (name.isEmpty() || m_playerNames.contains(name))
        return;
    m_playerNames.append(name);
}

void ClientGameState::removePlayer(const QString &name)
{
    m_playerNames.removeAll(name);
    m_alive.remove(name);
}

bool ClientGameState::hasPlayer(const QString &name) const
{
    return m_playerNames.contains(name);
}

void ClientGameState::setPlayerAlive(const QString &name, bool alive)
{
    if (name.isEmpty())
        return;
    m_alive.insert(name, alive);
}

bool ClientGameState::isPlayerAlive(const QString &name) const
{
    // 未收過死亡通知就當生勾勾:寧可唔攔,都唔可以攔錯。
    return m_alive.value(name, true);
}

void ClientGameState::setCardIdSpace(int count)
{
    m_cardIdSpace = count;
}

bool ClientGameState::isKnownCardId(int cardId) const
{
    if (cardId < 0)
        return false;
    if (m_cardIdSpace <= 0)
        return true;
    return cardId < m_cardIdSpace;
}

QJsonObject ClientGameState::toJson() const
{
    QJsonArray players;
    foreach (const QString &name, m_playerNames)
        players.append(name);

    QJsonObject object;
    object.insert(QStringLiteral("self"), m_selfName);
    object.insert(QStringLiteral("players"), players);
    object.insert(QStringLiteral("card_id_space"), m_cardIdSpace);
    return object;
}

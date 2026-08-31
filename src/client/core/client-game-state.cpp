#include "client-game-state.h"

void ClientGameState::reset()
{
    m_connection.clear();
    m_setup.clear();
    m_selfName.clear();
    m_cardIdSpace = 0;
    resetGameplayState();
}

void ClientGameState::resetGameplayState()
{
    m_game.clear();
    m_playerNames.clear();
    m_players.clear();
    m_cards.clear();
    m_latestPayloads.clear();
    m_flowCounts.clear();
    m_presentationEvents.clear();
}

void ClientGameState::setConnectionValue(const QString &key, const QVariant &value)
{
    m_connection.insert(key, value);
}

QVariant ClientGameState::connectionValue(const QString &key) const
{
    return m_connection.value(key);
}

void ClientGameState::setSetup(const QVariantMap &setup)
{
    m_setup = setup;
}

void ClientGameState::setGameValue(const QString &key, const QVariant &value)
{
    m_game.insert(key, value);
}

QVariant ClientGameState::gameValue(const QString &key) const
{
    return m_game.value(key);
}

void ClientGameState::setSelfName(const QString &name)
{
    m_selfName = name;
    if (!name.isEmpty())
        ensurePlayer(name);
}

QVariantMap &ClientGameState::ensurePlayer(const QString &name)
{
    QVariantMap &value = m_players[name];
    value.insert(QStringLiteral("object_name"), name);
    if (!m_playerNames.contains(name))
        m_playerNames.append(name);
    if (!value.contains(QStringLiteral("alive")))
        value.insert(QStringLiteral("alive"), true);
    return value;
}

void ClientGameState::setPlayerNames(const QStringList &names)
{
    m_playerNames.clear();
    for (const QString &name : names) {
        if (name.isEmpty() || m_playerNames.contains(name))
            continue;
        m_playerNames.append(name);
        ensurePlayer(name);
    }
}

void ClientGameState::addPlayer(const QString &name)
{
    if (!name.isEmpty())
        ensurePlayer(name);
}

void ClientGameState::removePlayer(const QString &name)
{
    m_playerNames.removeAll(name);
    QVariantMap &value = m_players[name];
    value.insert(QStringLiteral("object_name"), name);
    value.insert(QStringLiteral("removed"), true);
    value.insert(QStringLiteral("alive"), false);
}

bool ClientGameState::hasPlayer(const QString &name) const
{
    return m_players.contains(name) && !m_players.value(name).value(
        QStringLiteral("removed")).toBool();
}

void ClientGameState::setPlayerValue(const QString &name, const QString &key,
                                     const QVariant &value)
{
    if (!name.isEmpty())
        ensurePlayer(name).insert(key, value);
}

QVariant ClientGameState::playerValue(const QString &name, const QString &key) const
{
    return m_players.value(name).value(key);
}

QVariantMap ClientGameState::player(const QString &name) const
{
    return m_players.value(name);
}

void ClientGameState::setPlayerMark(const QString &name, const QString &mark, int value)
{
    QVariantMap &playerState = ensurePlayer(name);
    QVariantMap marks = playerState.value(QStringLiteral("marks")).toMap();
    if (value == 0)
        marks.remove(mark);
    else
        marks.insert(mark, value);
    playerState.insert(QStringLiteral("marks"), marks);
}

void ClientGameState::setPlayerAlive(const QString &name, bool alive)
{
    setPlayerValue(name, QStringLiteral("alive"), alive);
}

bool ClientGameState::isPlayerAlive(const QString &name) const
{
    return m_players.value(name).value(QStringLiteral("alive"), true).toBool();
}

void ClientGameState::setCardIdSpace(int count)
{
    m_cardIdSpace = qMax(0, count);
}

bool ClientGameState::isKnownCardId(int cardId) const
{
    if (cardId < 0)
        return false;
    if (m_cardIdSpace <= 0)
        return true;
    return cardId < m_cardIdSpace || m_cards.contains(cardId);
}

QVariantMap &ClientGameState::ensureCard(int cardId)
{
    QVariantMap &value = m_cards[cardId];
    value.insert(QStringLiteral("id"), cardId);
    return value;
}

void ClientGameState::setCardValue(int cardId, const QString &key, const QVariant &value)
{
    if (cardId >= 0)
        ensureCard(cardId).insert(key, value);
}

QVariantMap ClientGameState::card(int cardId) const
{
    return m_cards.value(cardId);
}

QList<int> ClientGameState::cardsForPlayer(const QString &playerName, int place) const
{
    QList<int> result;
    for (auto it = m_cards.constBegin(); it != m_cards.constEnd(); ++it) {
        if (it.value().value(QStringLiteral("owner")).toString() == playerName
            && (place < 0 || it.value().value(QStringLiteral("place")).toInt() == place)) {
            result.append(it.key());
        }
    }
    return result;
}

void ClientGameState::recordFlow(int command, const QVariant &payload)
{
    m_latestPayloads.insert(command, payload);
    m_flowCounts.insert(command, m_flowCounts.value(command) + 1);
}

QVariant ClientGameState::latestPayload(int command) const
{
    return m_latestPayloads.value(command);
}

int ClientGameState::flowCount(int command) const
{
    return m_flowCounts.value(command);
}

void ClientGameState::appendPresentationEvent(int command, const QString &text,
                                              const QVariant &payload)
{
    QVariantMap event{{QStringLiteral("command"), command},
                      {QStringLiteral("text"), text}};
    if (payload.isValid())
        event.insert(QStringLiteral("payload"), payload);
    m_presentationEvents.append(event);
    constexpr int eventLimit = 200;
    while (m_presentationEvents.size() > eventLimit)
        m_presentationEvents.removeFirst();
}

QJsonObject ClientGameState::toJson() const
{
    QVariantList players;
    for (const QString &name : m_playerNames)
        players.append(m_players.value(name));
    for (auto it = m_players.constBegin(); it != m_players.constEnd(); ++it) {
        if (!m_playerNames.contains(it.key()))
            players.append(it.value());
    }
    QVariantList cards;
    for (auto it = m_cards.constBegin(); it != m_cards.constEnd(); ++it)
        cards.append(it.value());
    QVariantMap flowCounts;
    for (auto it = m_flowCounts.constBegin(); it != m_flowCounts.constEnd(); ++it)
        flowCounts.insert(QString::number(it.key()), it.value());

    const QVariantMap result{{QStringLiteral("connection"), m_connection},
        {QStringLiteral("setup"), m_setup}, {QStringLiteral("game"), m_game},
        {QStringLiteral("self_name"), m_selfName},
        {QStringLiteral("card_id_space"), m_cardIdSpace},
        {QStringLiteral("players"), players}, {QStringLiteral("cards"), cards},
        {QStringLiteral("flow_counts"), flowCounts},
        {QStringLiteral("presentation_events"), m_presentationEvents}};
    return QJsonObject::fromVariantMap(result);
}

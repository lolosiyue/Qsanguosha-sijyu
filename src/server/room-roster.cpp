#include "room-roster.h"

#include "serverplayer.h"

#include <QStringList>

void RoomRoster::add(ServerPlayer *player)
{
    m_players << player;
}

void RoomRoster::remove(ServerPlayer *player)
{
    m_players.removeOne(player);
    m_alivePlayers.removeOne(player);
}

void RoomRoster::insertAfter(ServerPlayer *before, ServerPlayer *player, bool addToAlive)
{
    m_players.insert(m_players.indexOf(before) + 1, player);
    if (addToAlive)
        m_alivePlayers.insert(m_alivePlayers.indexOf(before) + 1, player);
}

void RoomRoster::replacePlayers(const QList<ServerPlayer *> &players)
{
    m_players = players;
}

QList<ServerPlayer *> RoomRoster::players() const
{
    return m_players;
}

QList<ServerPlayer *> RoomRoster::alivePlayers() const
{
    return m_alivePlayers;
}

int RoomRoster::aliveCount() const
{
    return m_alivePlayers.count();
}

QList<ServerPlayer *> RoomRoster::orderedFrom(ServerPlayer *current, bool includeDead) const
{
    if (!current)
        return m_players;

    const int index = m_players.indexOf(current);
    if (index < 0)
        return m_players;

    QList<ServerPlayer *> orderedPlayers;
    for (int i = index; i < m_players.length(); ++i) {
        if (includeDead || m_players[i]->isAlive())
            orderedPlayers << m_players[i];
    }
    for (int i = 0; i < index; ++i) {
        if (includeDead || m_players[i]->isAlive())
            orderedPlayers << m_players[i];
    }
    return orderedPlayers;
}

QList<ServerPlayer *> RoomRoster::otherPlayers(ServerPlayer *current, ServerPlayer *except, bool includeDead) const
{
    QList<ServerPlayer *> otherPlayers = orderedFrom(current, includeDead);
    if (except)
        otherPlayers.removeOne(except);
    return otherPlayers;
}

ServerPlayer *RoomRoster::findByGeneral(const QString &generalName, bool includeDead) const
{
    const QList<ServerPlayer *> candidates = includeDead ? m_players : m_alivePlayers;
    if (generalName.contains("+")) {
        const QStringList names = generalName.split("+");
        foreach (ServerPlayer *player, candidates) {
            if (names.contains(player->getGeneralName()))
                return player;
        }
    } else {
        foreach (ServerPlayer *player, candidates) {
            if (player->getGeneralName() == generalName)
                return player;
        }
    }
    return nullptr;
}

ServerPlayer *RoomRoster::findByObjectName(const QString &objectName, ServerPlayer *current, bool includeDead) const
{
    foreach (ServerPlayer *player, orderedFrom(current, includeDead)) {
        if (player->objectName() == objectName)
            return player;
    }
    return nullptr;
}

QList<ServerPlayer *> RoomRoster::findBySkill(const QString &skillName, ServerPlayer *current) const
{
    QList<ServerPlayer *> playersWithSkill;
    foreach (ServerPlayer *player, orderedFrom(current, false)) {
        if (player->hasSkill(skillName))
            playersWithSkill << player;
    }
    return playersWithSkill;
}

ServerPlayer *RoomRoster::findFirstBySkill(const QString &skillName, ServerPlayer *current, bool includeLose) const
{
    foreach (ServerPlayer *player, orderedFrom(current, false)) {
        if (player->hasSkill(skillName, includeLose))
            return player;
    }
    return nullptr;
}

QList<ServerPlayer *> RoomRoster::pathBetween(ServerPlayer *from, ServerPlayer *to,
                                              bool includeFrom, bool includeTo) const
{
    if (!from || !to || from == to)
        return QList<ServerPlayer *>();

    const QList<ServerPlayer *> clockwise = clockwisePath(from, to, includeFrom, includeTo);
    const QList<ServerPlayer *> counterclockwise = counterclockwisePath(from, to, includeFrom, includeTo);
    return clockwise.length() <= counterclockwise.length() ? clockwise : counterclockwise;
}

QList<ServerPlayer *> RoomRoster::clockwisePath(ServerPlayer *from, ServerPlayer *to,
                                                bool includeFrom, bool includeTo) const
{
    QList<ServerPlayer *> path;
    if (!from || !to)
        return path;

    if (from == to) {
        if (includeFrom && includeTo)
            path << from;
        return path;
    }

    if (includeFrom)
        path << from;

    ServerPlayer *current = from->getNextAlive();
    while (current && current != to) {
        path << current;
        current = current->getNextAlive();
    }

    if (includeTo && current == to)
        path << to;
    return path;
}

QList<ServerPlayer *> RoomRoster::counterclockwisePath(ServerPlayer *from, ServerPlayer *to,
                                                       bool includeFrom, bool includeTo) const
{
    QList<ServerPlayer *> path;
    if (!from || !to)
        return path;

    if (from == to) {
        if (includeFrom && includeTo)
            path << from;
        return path;
    }

    if (includeTo)
        path << to;

    ServerPlayer *current = to->getNextAlive();
    while (current && current != from) {
        path << current;
        current = current->getNextAlive();
    }

    if (includeFrom && current == from)
        path << from;
    return path;
}

QList<ServerPlayer *> RoomRoster::alivePlayersAfter(ServerPlayer *player) const
{
    QList<ServerPlayer *> playersAfter;
    const int start = m_alivePlayers.indexOf(player) + 1;
    for (int i = start; i < m_alivePlayers.length(); ++i)
        playersAfter << m_alivePlayers[i];
    return playersAfter;
}

void RoomRoster::removeAlive(ServerPlayer *player)
{
    foreach (ServerPlayer *laterPlayer, alivePlayersAfter(player))
        laterPlayer->setSeat(laterPlayer->getSeat() - 1);
    m_alivePlayers.removeOne(player);
}

void RoomRoster::resetAliveToPlayers()
{
    m_alivePlayers = m_players;
}

void RoomRoster::rebuildAlive()
{
    m_alivePlayers.clear();
    foreach (ServerPlayer *player, m_players) {
        if (player->isAlive())
            m_alivePlayers << player;
    }
}

void RoomRoster::reseatAlive()
{
    for (int i = 0; i < m_alivePlayers.length(); ++i)
        m_alivePlayers[i]->setSeat(i + 1);
}

void RoomRoster::swapSeats(ServerPlayer *first, ServerPlayer *second)
{
    const int firstSeat = m_players.indexOf(first);
    const int secondSeat = m_players.indexOf(second);
    m_players.swapItemsAt(firstSeat, secondSeat);

    rebuildAlive();
    for (int i = 0; i < m_players.length(); ++i) {
        ServerPlayer *player = m_players[i];
        if (player->isAlive())
            player->setSeat(m_alivePlayers.indexOf(player) + 1);
        else
            player->setSeat(0);
        player->setPlayerSeat(i + 1);
    }
    relinkPlayers();
}

void RoomRoster::adjustSeats(bool keepOriginalStart)
{
    int firstIndex = 0;
    if (!keepOriginalStart) {
        foreach (ServerPlayer *player, m_players) {
            if (player->getRoleEnum() == Player::Lord)
                break;
            ++firstIndex;
        }
    }

    QList<ServerPlayer *> adjustedPlayers;
    for (int i = firstIndex; i < m_players.length(); ++i)
        adjustedPlayers << m_players.at(i);
    for (int i = 0; i < firstIndex; ++i)
        adjustedPlayers << m_players.at(i);
    m_players = adjustedPlayers;

    for (int i = 0; i < m_players.length(); ++i) {
        m_players[i]->setSeat(i + 1);
        m_players[i]->setPlayerSeat(i + 1);
    }
}

void RoomRoster::reversePlayOrder()
{
    m_playOrderReversed = !m_playOrderReversed;
    if (m_players.length() >= 2)
        relinkPlayers();
}

bool RoomRoster::isPlayOrderReversed() const
{
    return m_playOrderReversed;
}

void RoomRoster::relinkPlayers()
{
    const int count = m_players.length();
    if (count == 0)
        return;

    for (int i = 0; i < count; ++i) {
        const int nextIndex = m_playOrderReversed ? (i - 1 + count) % count : (i + 1) % count;
        m_players[i]->setNext(m_players[nextIndex]);
    }
}

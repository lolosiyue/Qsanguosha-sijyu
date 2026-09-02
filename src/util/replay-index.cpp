#include "replay-index.h"
#include "protocol.h"
#include "json.h"

#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>

#include <limits>

using namespace QSanProtocol;
using namespace QSanReplay;

ReplayIndex::ReplayIndex(QObject *parent)
    : QObject(parent), m_lastTurnCount(0)
{
}

void ReplayIndex::buildIndex(const QList<ReplayEvent> &events)
{
    clear();
    m_lastTurnCount = 0;

    for (int i = 0; i < events.size(); i++) {
        const ReplayEvent &event = events.at(i);
        ReplayNode node;
        node.pairIndex = i;
        node.elapsed = event.elapsedMs;

        if (parseMessage(event.message, node)) {
            m_nodes.append(node);
            int nodeIndex = m_nodes.size() - 1;

            if (node.type == ReplayNodeType::TurnStart) {
                m_turnToNodeMap[node.turnCount] = nodeIndex;
            }
            m_elapsedToNodeMap[node.elapsed] = nodeIndex;
        }
    }

    loadSnapshots();
}

void ReplayIndex::clear()
{
    m_nodes.clear();
    m_turnToNodeMap.clear();
    m_elapsedToNodeMap.clear();
}

QList<ReplayNode> ReplayIndex::getNodes() const
{
    return m_nodes;
}

ReplayNode ReplayIndex::getNode(int index) const
{
    if (index >= 0 && index < m_nodes.size())
        return m_nodes[index];
    return ReplayNode();
}

int ReplayIndex::getNodeCount() const
{
    return m_nodes.size();
}

int ReplayIndex::findNodeByElapsed(qint64 elapsed) const
{
    int bestMatch = -1;
    qint64 bestDiff = std::numeric_limits<qint64>::max();

    for (int i = 0; i < m_nodes.size(); i++) {
        const qint64 nodeElapsed = m_nodes.at(i).elapsed;
        const qint64 diff = nodeElapsed >= elapsed
            ? nodeElapsed - elapsed : elapsed - nodeElapsed;
        if (diff < bestDiff) {
            bestDiff = diff;
            bestMatch = i;
        }
    }

    return bestMatch;
}

int ReplayIndex::findNodeByTurn(int turnCount) const
{
    if (m_turnToNodeMap.contains(turnCount))
        return m_turnToNodeMap[turnCount];
    return -1;
}

int ReplayIndex::findNearestNode(int pairIndex) const
{
    int bestMatch = -1;
    int bestDiff = INT_MAX;

    for (int i = 0; i < m_nodes.size(); i++) {
        int diff = qAbs(m_nodes[i].pairIndex - pairIndex);
        if (diff < bestDiff) {
            bestDiff = diff;
            bestMatch = i;
        }
    }

    return bestMatch;
}

void ReplayIndex::setSnapshotPath(const QString &path)
{
    m_snapshotPath = path;
    loadSnapshots();
}

QString ReplayIndex::getSnapshotPath() const
{
    return m_snapshotPath;
}

QString ReplayIndex::getNodeDescription(const ReplayNode &node) const
{
    switch (node.type) {
    case ReplayNodeType::TurnStart:
        return tr("Turn %1").arg(node.turnCount);
    case ReplayNodeType::PlayerDeath:
        return tr("%1 died").arg(node.playerName);
    case ReplayNodeType::GameOver:
        return tr("Game Over");
    }
    return QString();
}

bool ReplayIndex::parseMessage(const ProtocolMessage &message, ReplayNode &node)
{
    const CommandType commandType = static_cast<CommandType>(message.command);
    const QVariant &body = message.payload;

    switch (commandType) {
    case S_COMMAND_SET_MARK: {
        const QVariantMap object = body.toMap();
        const QString mark = object.value(QStringLiteral("mark_name")).toString();
        if (mark == "Global_TurnCount") {
            node.type = ReplayNodeType::TurnStart;
            node.playerName = object.value(QStringLiteral("player_name")).toString();
            node.playerTurnCount = object.value(QStringLiteral("value")).toInt();
            node.turnCount = ++m_lastTurnCount;
            node.description = tr("Turn %1").arg(node.turnCount);
            return !node.playerName.isEmpty() && node.playerTurnCount > 0;
        }
        break;
    }

    case S_COMMAND_LOG_SKILL: {
        const QVariantMap log = body.toMap();
        QStringList recipients;
        if (JsonUtils::tryParse(log.value(QStringLiteral("to_players")), recipients)) {
            const QString type = log.value(QStringLiteral("log_type")).toString();
            if (type == "#Murder" || type == "#Suicide" || type == "#Contingency") {
                node.type = ReplayNodeType::PlayerDeath;
                if (!recipients.isEmpty()) {
                    node.playerName = recipients.first();
                    node.description = tr("%1 died").arg(node.playerName);
                    node.turnCount = m_lastTurnCount;
                    return true;
                }
            }
        }
        break;
    }

    case S_COMMAND_GAME_OVER: {
        node.type = ReplayNodeType::GameOver;
        node.description = tr("Game Over");
        node.turnCount = m_lastTurnCount;
        return true;
    }

    default:
        break;
    }

    return false;
}

void ReplayIndex::loadSnapshots()
{
    // Snapshot discovery is deliberately owned by Replayer's strict manifest
    // verifier. ReplayIndex must never revive the old directory-scan contract.
    for (ReplayNode &node : m_nodes)
        node.snapshotIndex = -1;
}

#include "replay-index.h"
#include "protocol.h"
#include "json.h"

#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>

using namespace QSanProtocol;

ReplayIndex::ReplayIndex(QObject *parent)
    : QObject(parent), m_lastTurnCount(0)
{
}

void ReplayIndex::buildIndex(const QList<QPair<int, QString>> &pairs)
{
    clear();
    m_lastTurnCount = 0;

    for (int i = 0; i < pairs.size(); i++) {
        const QPair<int, QString> &pair = pairs[i];
        ReplayNode node;
        node.pairIndex = i;
        node.elapsed = pair.first;

        if (parsePacket(pair.second, node)) {
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

int ReplayIndex::findNodeByElapsed(int elapsed) const
{
    int bestMatch = -1;
    int bestDiff = INT_MAX;

    for (int i = 0; i < m_nodes.size(); i++) {
        int diff = qAbs(m_nodes[i].elapsed - elapsed);
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

bool ReplayIndex::parsePacket(const QString &cmd, ReplayNode &node)
{
    Packet packet;
    if (!packet.parse(cmd.toLatin1().constData()))
        return false;

    CommandType commandType = packet.getCommandType();
    const QVariant &body = packet.getMessageBody();

    switch (commandType) {
    case S_COMMAND_SET_MARK: {
        JsonArray args = body.value<JsonArray>();
        if (args.size() >= 3) {
            QString mark = args[1].toString();
            if (mark == "Global_TurnCount") {
                int turnCount = args[2].toInt();
                if (turnCount > m_lastTurnCount) {
                    node.type = ReplayNodeType::TurnStart;
                    node.turnCount = turnCount;
                    node.description = tr("Turn %1").arg(turnCount);
                    m_lastTurnCount = turnCount;
                    return true;
                }
            }
        }
        break;
    }

    case S_COMMAND_LOG_SKILL: {
        QStringList log;
        if (JsonUtils::tryParse(body, log) && log.size() >= 3) {
            const QString &type = log.at(0);
            if (type == "#Murder" || type == "#Suicide" || type == "#Contingency") {
                node.type = ReplayNodeType::PlayerDeath;
                QStringList tos = log.at(2).split('+');
                if (!tos.isEmpty()) {
                    node.playerName = tos.first();
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
    if (m_snapshotPath.isEmpty())
        return;

    QDir dir(m_snapshotPath);
    if (!dir.exists())
        return;

    QStringList filters;
    filters << "*.json";
    QStringList files = dir.entryList(filters, QDir::Files, QDir::Name);

    foreach (const QString &file, files) {
        QString filepath = m_snapshotPath + "/" + file;

        QFile f(filepath);
        if (!f.open(QIODevice::ReadOnly))
            continue;

        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();

        QVariantMap root = doc.toVariant().toMap();
        int turnCount = root["turnCount"].toInt();
        QString type = root["snapshotType"].toString();

        for (int i = 0; i < m_nodes.size(); i++) {
            if (m_nodes[i].turnCount == turnCount) {
                bool match = false;
                if (type == "turn" && m_nodes[i].type == ReplayNodeType::TurnStart)
                    match = true;
                else if (type == "death" && m_nodes[i].type == ReplayNodeType::PlayerDeath)
                    match = true;

                if (match) {
                    m_nodes[i].snapshotIndex = i;
                    break;
                }
            }
        }
    }
}
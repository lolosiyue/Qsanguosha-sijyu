#ifndef _REPLAY_INDEX_H
#define _REPLAY_INDEX_H

#include <QObject>
#include <QList>
#include <QString>
#include <QMap>

class Replayer;

enum class ReplayNodeType
{
    TurnStart,
    PlayerDeath,
    GameOver
};

struct ReplayNode
{
    int pairIndex;
    int elapsed;
    ReplayNodeType type;
    QString description;
    QString playerName;
    int turnCount;
    int snapshotIndex;

    ReplayNode() : pairIndex(-1), elapsed(0), type(ReplayNodeType::TurnStart),
                   turnCount(0), snapshotIndex(-1) {}
};

class ReplayIndex : public QObject
{
    Q_OBJECT

public:
    explicit ReplayIndex(QObject *parent = nullptr);

    void buildIndex(const QList<QPair<int, QString>> &pairs);
    void clear();

    QList<ReplayNode> getNodes() const;
    ReplayNode getNode(int index) const;
    int getNodeCount() const;

    int findNodeByElapsed(int elapsed) const;
    int findNodeByTurn(int turnCount) const;
    int findNearestNode(int pairIndex) const;

    void setSnapshotPath(const QString &path);
    QString getSnapshotPath() const;

    QString getNodeDescription(const ReplayNode &node) const;

private:
    bool parsePacket(const QString &cmd, ReplayNode &node);
    void loadSnapshots();

    QList<ReplayNode> m_nodes;
    QMap<int, int> m_turnToNodeMap;
    QMap<int, int> m_elapsedToNodeMap;
    QString m_snapshotPath;
    int m_lastTurnCount;
};

#endif
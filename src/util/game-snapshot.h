#ifndef _GAME_SNAPSHOT_H
#define _GAME_SNAPSHOT_H

#include <QObject>
#include <QDateTime>
#include <QVariantMap>
#include <QList>
#include <QMap>
#include <QString>

class Room;
class ServerPlayer;

struct PlayerSnapshot
{
    QString objectName;
    QString screenName;
    QString general;
    QString general2;
    QString kingdom;
    QString role;
    int hp;
    int maxhp;
    int seat;
    int playerSeat;
    bool alive;
    bool faceup;
    bool chained;
    bool owner;
    bool roleShown;
    bool generalShowed;
    bool general2Showed;
    QString gender;
    QString state;

    QList<int> handcards;
    QList<int> equips;
    QList<int> judgingArea;
    QMap<QString, QList<int>> piles;
    QMap<QString, int> marks;
    QStringList flags;
    QStringList skills;

    QMap<QString, int> history;
    QMap<int, int> equipAreas;

    QVariantMap serialize() const;
    static PlayerSnapshot deserialize(const QVariantMap &map);
    static PlayerSnapshot fromPlayer(ServerPlayer *player);
};

struct GlobalSnapshot
{
    int turnCount;
    int roundCount;
    QString currentPlayer;
    QString currentPhase;
    QString gameMode;
    QStringList packages;

    QList<int> drawPile;
    QList<int> discardPile;
    QList<PlayerSnapshot> players;
    QStringList seatOrder;

    QVariantMap roomTags;
    QStringList chatHistory;

    QVariantMap serialize() const;
    static GlobalSnapshot deserialize(const QVariantMap &map);
};

class GameSnapshot : public QObject
{
    Q_OBJECT

public:
    explicit GameSnapshot(QObject *parent = nullptr);
    explicit GameSnapshot(Room *room, QObject *parent = nullptr);
    explicit GameSnapshot(const QString &filepath, QObject *parent = nullptr);

    bool save(const QString &filepath);
    bool load(const QString &filepath);

    GlobalSnapshot getState() const;
    void setState(const GlobalSnapshot &state);

    int getTurnCount() const;
    void setTurnCount(int turn);
    QDateTime getTimestamp() const;
    QString getReplayPath() const;
    void setReplayPath(const QString &path);

    QString getSnapshotType() const;
    void setSnapshotType(const QString &type);
    QString getDescription() const;
    void setDescription(const QString &desc);

    static QString getSnapshotDir(const QString &replayPath);
    static QString generateSnapshotFilename(int turnCount, const QString &type, const QString &playerName = QString());

private:
    GlobalSnapshot m_state;
    QDateTime m_timestamp;
    QString m_replayPath;
    QString m_snapshotType;
    QString m_description;
};

#endif
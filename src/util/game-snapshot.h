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

namespace GameSnapshotTags {
// 技能好興喺 room／player tag 裡面擺一個 ServerPlayer*(房內另一名玩家)。指標
// 本身唔係 JSON 值, 但佢指嘅嘢喺 snapshot 內部已經有名(players[].objectName),
// 所以捕捉時換成 {"__player": "<objectName>"} 呢個單鍵標記無損咁記低,
// restore 再按名解返 runtime 指標(takeover-scenario.cpp)。
inline constexpr char PlayerRefKey[] = "__player";
}

// A distinct on-disk contract. Legacy partial snapshots must never be used
// for takeover: a node is either lossless or ineligible.
struct RngSnapshot
{
    QString algorithm;
    QString seed;
    QString drawCount;

    QVariantMap serialize() const;
    static RngSnapshot deserialize(const QVariantMap &map);
};

struct SkillInstanceSnapshot
{
    QString skillName;
    int instanceID = 0;
    int source = 0;
    QString parentSkillName;
    int parentInstanceID = 0;
    QString parentRefOwner;
    QString parentRefSkillName;
    int parentRefInstanceID = 0;
    bool visible = true;
    bool hasAmountOverride = false;
    int amountOverride = 0;
    int bindHead = 0;
    QVariantMap state;
    QVariantMap correctState;

    QVariantMap serialize() const;
    static SkillInstanceSnapshot deserialize(const QVariantMap &map);
};

struct CardSnapshot
{
    int id = -1;
    QString objectName;
    QString className;
    QString suit;
    int suitId = -1;
    int number = -1;
    QString skillName;
    int skillInstanceId = 0;
    QString sourceSkillName;
    int sourceSkillInstanceId = 0;
    QString activationSkillName;
    int activationSkillInstanceId = 0;
    bool modified = false;
    QStringList flags;
    QMap<QString, int> marks;
    QVariantMap tags;

    QVariantMap serialize() const;
    static CardSnapshot deserialize(const QVariantMap &map);
};

struct PlayerSnapshot
{
    QString objectName;
    QString screenName;
    QString general;
    QString general2;
    QString kingdom;
    QString role;
    int hp = 0;
    int maxhp = 0;
    int seat = 0;
    int playerSeat = 0;
    bool alive = false;
    bool faceup = true;
    bool chained = false;
    bool owner = false;
    bool roleShown = false;
    bool generalShowed = false;
    bool general2Showed = false;
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
    QVariantMap dynamicProperties;
    QVariantMap tags;
    QList<SkillInstanceSnapshot> skillInstances;

    QVariantMap serialize() const;
    static PlayerSnapshot deserialize(const QVariantMap &map);
    static PlayerSnapshot fromPlayer(ServerPlayer *player);

    bool operator==(const PlayerSnapshot &other) const { return objectName == other.objectName; }
};

struct GlobalSnapshot
{
    int turnCount = 0;
    int roundCount = 0;
    quint64 turnSerial = 0;
    QString currentPlayer;
    QString currentPhase;
    QString gameMode;
    QStringList packages;

    QList<int> drawPile;
    QList<int> discardPile;
    QList<PlayerSnapshot> players;
    QStringList seatOrder;

    // Every physical card and its current room mapping.
    QList<CardSnapshot> cards;
    QMap<int, int> cardPlaces;
    QMap<int, QString> cardOwners;

    QVariantMap roomTags;
    QStringList chatHistory;
    QVariantMap catalogFingerprint;
    QVariantMap configFingerprint;
    RngSnapshot gameplayRng;
    RngSnapshot aiRng;
    QVariantList pendingExtraTurns;
    QVariantMap luaTakeoverState;
    QStringList unsupportedState;
    bool eligible = true;
    QString ineligibleReason;

    QVariantMap serialize() const;
    static GlobalSnapshot deserialize(const QVariantMap &map);
};

class GameSnapshot : public QObject
{
    Q_OBJECT

public:
    static constexpr int TakeoverSchemaVersion = 2;
    static QString takeoverFormat();

    explicit GameSnapshot(QObject *parent = nullptr);
    explicit GameSnapshot(Room *room, QObject *parent = nullptr);
    explicit GameSnapshot(const QString &filepath, QObject *parent = nullptr);

    bool save(const QString &filepath);
    bool load(const QString &filepath);

    GlobalSnapshot getState() const;
    void setState(const GlobalSnapshot &state);

    int getTurnCount() const;
    void setTurnCount(int turn);
    quint64 getTurnSerial() const;
    void setTurnSerial(quint64 serial);
    QDateTime getTimestamp() const;
    QString getReplayPath() const;
    void setReplayPath(const QString &path);

    QString getSnapshotType() const;
    void setSnapshotType(const QString &type);
    QString getDescription() const;
    void setDescription(const QString &desc);
    bool isEligible() const;
    QString getError() const;

    static QString getSnapshotDir(const QString &replayPath);
    static QString generateSnapshotFilename(int turnCount, const QString &type, const QString &playerName = QString());
    static QVariantMap currentCatalogFingerprint();
    static QVariantMap currentConfigFingerprint(const QString &gameMode = QString());
    static bool validateRuntimeCompatibility(const GlobalSnapshot &state,
                                             QString *error = nullptr);

private:
    GlobalSnapshot m_state;
    QDateTime m_timestamp;
    QString m_replayPath;
    QString m_snapshotType;
    QString m_description;
    int m_turnCount = 0;
    QString m_error;
};

#endif

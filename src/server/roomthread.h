#ifndef _ROOM_THREAD_H
#define _ROOM_THREAD_H

#include "structs.h"

#include <QHash>
#include <QSet>

class GameRule;

struct LogMessage
{
    LogMessage();
    QVariant toVariant() const;

    QString type;
    ServerPlayer *from;
    QList<ServerPlayer *> to;
    QString card_str;
    QString arg;
    QString arg2;
    QString arg3;
    QString arg4;
    QString arg5;
};

class EventTriplet
{
public:
    inline EventTriplet(TriggerEvent triggerEvent, Room *room, ServerPlayer *target)
        : _m_event(triggerEvent), _m_room(room), _m_target(target)
    {
    }
    QString toString() const;

private:
    TriggerEvent _m_event;
    Room *_m_room;
    ServerPlayer *_m_target;
};

class RoomThread : public QThread
{
    Q_OBJECT

public:
    explicit RoomThread(Room *room);
    void constructTriggerTable();
    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *target, QVariant &data);
    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *target);
    // Diagnostic snapshot; call from the RoomThread or after it has stopped.
    QVariantMap triggerDispatchProfile() const;

    // Invalidates only the client-facing distanceTo_* synchronization cache.
    // Server-side game rules continue to call Player::distanceTo() directly.
    void markDistanceCacheDirty(const char *source = nullptr);

    void addPlayerSkills(ServerPlayer *player, bool invoke_game_start = false);

    void addTriggerSkill(const TriggerSkill *skill);
    void delay(long msecs = -1);
    ServerPlayer *find3v3Next(QList<ServerPlayer *> &first, QList<ServerPlayer *> &second);
    void run3v3(QList<ServerPlayer *> &first, QList<ServerPlayer *> &second, GameRule *game_rule, ServerPlayer *current);
    void actionHulaoPass(ServerPlayer *shenlvbu, QList<ServerPlayer *> league, GameRule *game_rule, int stage);
    ServerPlayer *findHulaoPassNext(ServerPlayer *shenlvbu, QList<ServerPlayer *> league, int stage);
    void actionNormal(GameRule *game_rule);

    const QList<EventTriplet> *getEventStack() const;

protected:
    virtual void run();

private:
    struct DistanceRefreshProfile {
        quint64 flushCount = 0;
        quint64 orderedPairCount = 0;
        quint64 changedPropertyCount = 0;
        qint64 totalElapsedNs = 0;
        qint64 maxFlushNs = 0;
        qint64 distanceCalculationNs = 0;
        qint64 comparisonNs = 0;
        qint64 propertySyncNs = 0;
        QHash<QString, quint64> dirtySourceCounts;
        QHash<QString, quint64> flushSourceCounts;
    };

    struct TriggerDispatchProfile {
        quint64 triggerCount = 0;
        quint64 priorityRebuildCount = 0;
        quint64 prioritySkillCount = 0;
        quint64 prioritySortCount = 0;
        quint64 v2DispatchCount = 0;
        quint64 v2EmptyDispatchCount = 0;
        quint64 v2CandidateCount = 0;
        quint64 mainTableCandidateVisitCount = 0;
    };

    struct TriggerSkillTraits {
        bool v2 = false;
        bool equipOrRule = false;
        bool gameRule = false;
    };

    void _handleTurnBroken3v3(QList<ServerPlayer *> &first, QList<ServerPlayer *> &second, GameRule *game_rule);
    void _handleTurnBrokenHulaoPass(ServerPlayer *shenlvbu, QList<ServerPlayer *> league, GameRule *game_rule, int stage);
    void _handleTurnBrokenNormal(GameRule *game_rule);
    bool triggerV2Skills(TriggerEvent triggerEvent, Room *room, ServerPlayer *target, QVariant &data);
    void sortTriggerSkills(TriggerEvent triggerEvent, Room *room, bool includeLose);
    void refreshDistanceCacheIfDirty(Room *room);
    void flushOutermostDeferredWork(Room *room);
    void emitDistanceRefreshProfile() const;
    const QByteArray &distancePropertyName(const ServerPlayer *player);

    Room *room;
    bool m_playerUiStateDirty = false;
    bool m_distanceCacheDirty = false;
    bool m_distanceRefreshProfilingEnabled;
    int m_profileRoomId;
    QString m_profileMode;
    DistanceRefreshProfile m_distanceRefreshProfile;
    TriggerDispatchProfile m_triggerDispatchProfile;
    QSet<QString> m_pendingDistanceDirtySources;
    QHash<const ServerPlayer *, QByteArray> m_distancePropertyNames;
    QHash<const ServerPlayer *, QHash<const ServerPlayer *, int>> m_lastBroadcastDistances;
    QString order;

    QList<TriggerSkill *> skill_table[NumOfEvents];
    QList<TriggerSkill *> v2_skill_table[NumOfEvents];
    QList<const TriggerSkill *> skillSet;
    QHash<const TriggerSkill *, TriggerSkillTraits> m_triggerSkillTraits;

    QList<EventTriplet> event_stack;
};

#endif

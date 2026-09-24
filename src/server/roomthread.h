#ifndef _ROOM_THREAD_H
#define _ROOM_THREAD_H

#include "structs.h"

#include <QHash>
#include <QSet>
#include <atomic>

class GameRule;
struct SkillContext;

// Opt-in attached legacy skills use this only around an accepted effect. The
// enclosing dispatcher pins source identity before prompts or nested events.
class LegacySkillActivation
{
public:
    LegacySkillActivation(Room *room, ServerPlayer *owner, const QString &skillName);
    ~LegacySkillActivation() noexcept(false);
    explicit operator bool() const { return m_allowed; }
    static bool isAvailable(Room *room, ServerPlayer *owner, const QString &skillName);
    LegacySkillActivation(const LegacySkillActivation &) = delete;
    LegacySkillActivation &operator=(const LegacySkillActivation &) = delete;

private:
    Room *m_room;
    SkillContext *m_context = nullptr;
    bool m_allowed = true;
    int m_uncaughtExceptions;
};

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
    TriggerEvent event() const { return _m_event; }

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
    // Initialize only newly granted sources through the ordinary V2 lifecycle.
    bool triggerSkillSources(TriggerEvent event, Room *room, ServerPlayer *target, QVariant &data,
                             const QList<SkillInstanceRef> &sources);
    // Diagnostic snapshot; call from the RoomThread or after it has stopped.
    QVariantMap triggerDispatchProfile() const;

    // Invalidates only the client-facing distanceTo_* synchronization cache.
    // Server-side game rules continue to call Player::distanceTo() directly.
    void markDistanceCacheDirty();
    // Coalesce presentation work inside triggers; requests flush before input.
    bool deferPlayerUiState(ServerPlayer *player);
    void flushPlayerUiState();
    // Preserve interrupted lifecycle IDs while TurnBroken cleanup unwinds.
    void rememberInterruptedTurn(qint64 eventId);
    qint64 interruptedTurn() const;
    void clearInterruptedTurn();
    qint64 takeInterruptedTurn();
    void rememberInterruptedPhase(qint64 eventId);
    qint64 interruptedPhase() const;
    void clearInterruptedPhase();
    qint64 takeInterruptedPhase();
    void markSkillDescriptionsDirty();
    void refreshSkillDescriptions();

    // A nested card can continue an accepted legacy effect without consuming
    // the same attached source a second time.
    bool isLegacySkillActivationActive(const SkillInstanceRef &source) const;

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
    friend class LegacySkillActivation;
    struct LegacyExecutionFrame {
        QString skillName;
        TriggerEvent event;
        ServerPlayer *target = nullptr;
        QVariant *data = nullptr;
        QHash<QString, QList<SkillInstanceRef>> sources;
    };
    QList<LegacyExecutionFrame> m_legacyExecutionFrames;
    QList<SkillInstanceRef> m_activeLegacySources;

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
    bool dispatchTrigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *target, QVariant &data);
    void reclaimCompletedTurn();
    bool triggerV2Skills(TriggerEvent triggerEvent, Room *room, ServerPlayer *target, QVariant &data,
                         const QList<TriggerSkill *> *equipmentGroup = nullptr,
                         const QList<SkillInstanceRef> *allowedSources = nullptr);
    void sortTriggerSkills(TriggerEvent triggerEvent, Room *room, bool includeLose);
    void refreshDistanceCacheIfDirty(Room *room);
    void flushOutermostDeferredWork(Room *room);
    void emitPerfTrace() const;
    const QByteArray &distancePropertyName(const ServerPlayer *player);

    Room *room;
    bool m_playerUiStateDirty = false;
    bool m_flushingPlayerUiState = false;
    QSet<ServerPlayer *> m_pendingPlayerUiState;
    std::atomic_bool m_skillDescriptionsDirty{true};
    bool m_refreshingSkillDescriptions = false;
    bool m_distanceCacheDirty = false;
    bool m_perfTraceEnabled;
    int m_profileRoomId;
    QString m_profileMode;
    TriggerDispatchProfile m_triggerDispatchProfile;
    QHash<const ServerPlayer *, QByteArray> m_distancePropertyNames;
    QHash<const ServerPlayer *, QHash<const ServerPlayer *, int>> m_lastBroadcastDistances;
    qint64 m_interruptedTurnEventId = 0;
    qint64 m_interruptedPhaseEventId = 0;
    QString order;

    QList<TriggerSkill *> skill_table[NumOfEvents];
    quint64 m_triggerTableRevision[NumOfEvents] = {};
    QList<TriggerSkill *> v2_skill_table[NumOfEvents];
    QList<const TriggerSkill *> skillSet;
    QHash<const TriggerSkill *, TriggerSkillTraits> m_triggerSkillTraits;

    QList<EventTriplet> event_stack;
};

#endif

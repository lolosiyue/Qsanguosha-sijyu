#ifndef QSAN_ROOM_RUNTIME_H
#define QSAN_ROOM_RUNTIME_H

#include "engine-runtime-context.h"
#include "ai-runtime.h"
#include "game-rng.h"
#include "lua-runtime.h"
#include "room-definition-registry.h"
#include "room-state.h"
#include "skill-execution-registry.h"
#include "skill-instance-attachment-registry.h"

#include <QSet>
#include <QHash>

#include <atomic>
#include <memory>

class CardPattern;
class General;
class LuaSkillCard;
class Package;
class Room;
struct CardLifetimeGauge;
struct CardLifetimeToken;

class RoomRuntime : public EngineRuntimeContext
{
public:
    enum class ShutdownState : quint8 {
        Running,
        Closing,
        Closed,
        Failed
    };

    enum StateMutation {
        CardsMoved,
        PlayerPropertyChanged,
        PlayerLifecycleChanged,
        PlayerMarkChanged,
        SkillSetChanged,
        TurnStateChanged,
        CardLimitationChanged,
        SkillInstanceStateChanged
    };

    explicit RoomRuntime(Room *room);
    ~RoomRuntime() override;

    bool initialize(QString *error = nullptr);
    void shutdownForInitFailure();
    void finalizeWorker();
    void shutdownFinal();
    ShutdownState shutdownState() const { return m_shutdownState.load(); }
    bool isClosing() const { return shutdownState() != ShutdownState::Running; }
    bool isLoadingDefinitions() const { return m_loadingDefinitions; }
    bool definitionsLoaded() const { return m_definitionsLoaded; }
    void finishDefinitionLoading() { m_definitionsLoaded = true; }

    RoomDefinitionRegistry &definitions() { return m_definitions; }
    const RoomDefinitionRegistry &definitions() const { return m_definitions; }

    LuaRuntime &lua() { return m_lua; }
    const LuaRuntime &lua() const { return m_lua; }
    AiLuaRuntime &ai() { return m_ai; }
    const AiLuaRuntime &ai() const { return m_ai; }
    GameRng &rng() { return m_rng; }
    RoomState &state() { return m_roomState; }
    SkillExecutionRegistry &skillExecutions() { return m_skillExecutions; }
    SkillInstanceAttachmentRegistry &attachedSkills() { return m_attachedSkills; }
    quint64 nextDecisionId() { return ++m_nextDecisionId; }
    quint64 stateRevision() const { return m_stateRevision; }
    void advanceStateRevision(StateMutation mutation);
    void seedRandom(quint64 seed);

    void addPackage(Package *package);
    void setPackage(Package *package);
    void addSkills(const QList<const Skill *> &skills);
    void addTranslationEntry(const QString &key, const QString &value);
    QString translate(const QString &key, bool initial = false) const;
    bool hasTranslation(const QString &key) const;

    const Package *package(const QString &name) const;
    Package *packageOverlay(const Package *basePackage);
    QList<const Package *> packages() const;
    const General *general(const QString &name) const;
    QList<const General *> generals() const;
    const Skill *skill(const QString &name) const;
    QStringList skillNames() const;
    QList<const Skill *> allSkills() const;
    const Card *engineCard(int id) const;
    const Card *cardTemplate(const QString &name) const;
    int cardCount(int bootstrapCount) const;
    const CardPattern *pattern(const QString &name) const;
    QStringList relatedSkillNames(const QString &name) const;

    QList<const ProhibitSkill *> prohibitSkills() const;
    QList<const DistanceSkill *> distanceSkills() const;
    QList<const MaxCardsSkill *> maxCardsSkills() const;
    QList<const TargetModSkill *> targetModSkills() const;
    QList<const InvaliditySkill *> invaliditySkills() const;
    QList<const TriggerSkill *> globalTriggerSkills() const;
    QList<const AttackRangeSkill *> attackRangeSkills() const;
    QList<const ViewAsEquipSkill *> viewAsEquipSkills() const;
    QList<const CardLimitSkill *> cardLimitSkills() const;
    QList<const ProhibitPindianSkill *> prohibitPindianSkills() const;

    void registerLuaSkillCard(const QString &name, const LuaSkillCard *card);
    const LuaSkillCard *luaSkillCard(const QString &name) const;

    QObject *runtimeObject() override;
    RoomState *roomState() override { return &m_roomState; }
    const Player *cardOwner(int) const override { return nullptr; }
    Player::Place cardPlace(int) const override { return Player::PlaceUnknown; }
    Card *card(int cardId) const override { return m_roomState.getCard(cardId); }
    RoomRuntime *roomRuntime() override { return this; }

private:
    bool onCanonicalOwner() const;
    quint64 drainShutdownStage(const char *stage);
    void releaseShutdownRoots();
    bool finalGaugeIsZero(const CardLifetimeGauge &gauge) const;
    void failShutdown(const char *stage, const CardLifetimeGauge &gauge);
    void emitFinalGauge(const CardLifetimeGauge &gauge);
    void restorePreviousDomain();
    void invalidateOrphanedRuntimeEntries();

    Room *m_room;
    RoomDefinitionRegistry m_definitions;
    LuaRuntime m_lua;
    AiLuaRuntime m_ai;
    GameRng m_rng;
    RoomState m_roomState;
    SkillExecutionRegistry m_skillExecutions;
    SkillInstanceAttachmentRegistry m_attachedSkills;
    bool m_loadingDefinitions;
    bool m_definitionsLoaded;
    quint64 m_nextDecisionId;
    quint64 m_stateRevision;
    std::atomic<ShutdownState> m_shutdownState {ShutdownState::Running};
    bool m_finalMarkerEmitted = false;
    const void *m_previousDomain = nullptr;
    std::atomic_bool m_domainRestored {false};
    quint64 m_baselineManagedLive = 0;
    quint64 m_baselinePendingDelete = 0;
    quint64 m_baselineAdoptionReserved = 0;
    quint64 m_baselineWrapperLeases = 0;
    quint64 m_baselineNativeLeases = 0;
    quint64 m_baselineLuaPinsGauge = 0;
    quint64 m_baselineSidecarEdges = 0;
    quint64 m_baselineEntries = 0;
    quint64 m_baselineActiveScopes = 0;
    quint64 m_baselineLuaPins = 0;
    quint64 m_baselineActuallyDestroyed = 0;
    quint64 m_baselineCloneCreated = 0;
    quint64 m_baselineFactoryUnclaimed = 0;
    quint64 m_baselineUnknownUnclaimed = 0;
    const void *m_gameRuntimeIdentity = nullptr;
    lua_State *m_gameRuntimeState = nullptr;
    quint64 m_gameRuntimeGeneration = 0;
    const void *m_aiRuntimeIdentity = nullptr;
    lua_State *m_aiRuntimeState = nullptr;
    quint64 m_aiRuntimeGeneration = 0;
    QSet<const void *> m_baselineAddresses;
    QHash<const void *, std::shared_ptr<const CardLifetimeToken>> m_baselineTokenEntries;
    QHash<const void *, std::shared_ptr<const CardLifetimeToken>> m_runtimeObservedEntries;
};

#endif

#ifndef QSAN_GAME_STATE_CONTRACT_H
#define QSAN_GAME_STATE_CONTRACT_H

#include "game-rng.h"
#include "game-timeline.h"
#include <QMap>
#include <QSet>
#include <QVariantMap>
#include <functional>
#include <memory>

namespace GameState {

inline constexpr int SchemaVersion = 1;
// Schema 1 is an in-memory value-world contract, not GlobalSnapshot/takeover schema 3.
struct PlayerState {
    QString id;
    int hp = 0;
    int maxHp = 0;
    int armor = 0;
    bool alive = true;
    bool faceUp = true;
    bool chained = false;
    QMap<QString, int> marks;
    QMap<QString, int> usageHistory;
    QVariantMap properties;
    QVariantMap tags;
};
struct CardState {
    QString id;
    QString definition;
    int suit = 0;
    int number = 0;
    QVariantMap state;
};
struct SkillState {
    QString id; // Persistent instance ID, not a pointer or definition name.
    QString definition;
    QString ownerId;
    QString providerId;
    QString parentId;
    QVariantMap state;
    QVariantMap correctState;
};
enum class ZoneKind { Draw, Discard, Table, Hand, Equip, Judge, Private, Void };
struct CardZone {
    QString id;
    ZoneKind kind = ZoneKind::Void;
    QString ownerId;
    QString name;
    QStringList cards; // Order is authoritative, including private piles and equipment.
};
struct HistoryEvent {
    QString id;
    QString parentId;
    QString kind;
    QVariantMap data;
};
struct HistoryState {
    int version = 1;
    QVector<HistoryEvent> events;
    QStringList activeEventIds;
    quint64 nextEventId = 1;
};
struct TurnState {
    QString playerId;
    QString phase;
    QString turnScopeId;
    QString roundScopeId;
    bool extraTurn = false;
    // Only quiescent boundaries are supported by the initial executor. Suspended
    // C++/Lua stacks cannot be represented by simply copying a pending-turn list.
    QStringList continuations;
    QVariantList pendingExtraTurns;
    QVariantList pendingRequests;
    QVariantList timers;
};
struct ProviderState {
    int version = 0;
    QVariantMap state;
};
struct WorldState {
    int version = SchemaVersion;
    QString rootGameId;
    QString worldId;
    QStringList completeDomains;
    // Only explicitly audited package name + implementation version pairs work.
    QMap<QString, QString> nativePackages;
    QStringList unsupportedCapabilities;
    QMap<QString, PlayerState> players;
    QStringList seatOrder;
    QMap<QString, CardState> cards;
    QVector<CardZone> zones;
    QMap<QString, SkillState> skills;
    QVariantMap roomTags;
    TurnState turn;
    GameRng::State gameplayRng;
    GameRng::State aiRng;
    HistoryState history;
    QMap<QString, ProviderState> providers;
};

struct ProviderContract {
    QString id;
    int version = 0;
    QString audit; // Explicit justification: code/closure is immutable or rebuilt.
    QStringList skillDefinitions; // Exhaustive managed definitions owned by this provider.
    QStringList unsupportedCapabilities;
    // Trusted, audited construction hook. It receives only detached values; it
    // MUST NOT capture/mutate live Room, Lua states, sockets or external resources.
    std::function<bool(WorldState &, QString *)> prepare;
    std::function<bool(const WorldState &, QString *)> validate;
};
class ProviderRegistry {
public:
    bool registerProvider(const ProviderContract &contract, QString *error = nullptr);
    bool registerNativePackage(const QString &name, const QString &version,
                               const QString &audit, QString *error = nullptr);
    const QMap<QString, ProviderContract> &providers() const { return m_providers; }
    const QMap<QString, QString> &packages() const { return m_packages; }
private:
    QMap<QString, ProviderContract> m_providers;
    QMap<QString, QString> m_packages;
};

QStringList requiredDomains();
QVariantMap reference(const QString &kind, const QString &id);
// No QVariant pointer/userdata, implicit type coercion, non-finite number, or
// dangling/unknown reference. Pure trees have value semantics; aliases/metatables
// must be rejected by the Lua capture adapter before reaching QVariant.
bool validateValue(const QVariant &value, const WorldState &world, QString *error = nullptr);
bool validateWorld(const WorldState &world, const ProviderRegistry &registry,
                   QString *error = nullptr);

class WorldStore {
public:
    struct CheckpointRetention {
        int playerTurns = 8;
        int fullRounds = 4;
        int otherAnchors = 2;
    };
    WorldStore(const WorldStore &) = delete;
    WorldStore &operator=(const WorldStore &) = delete;
    WorldStore(WorldStore &&) = delete;
    WorldStore &operator=(WorldStore &&) = delete;
    class Candidate {
    public:
        Candidate(Candidate &&) noexcept = default;
        Candidate &operator=(Candidate &&) noexcept = default;
        Candidate(const Candidate &) = delete;
        Candidate &operator=(const Candidate &) = delete;
        const WorldState &state() const { return *m_state; }
    private:
        Candidate() = default;
        const WorldStore *m_owner = nullptr;
        quint64 m_stateRevision = 0;
        quint64 m_timelineRevision = 0;
        std::shared_ptr<const WorldState> m_state;
        std::unique_ptr<GameTimeline> m_timeline;
        QMap<QString, std::shared_ptr<const WorldState>> m_checkpoints;
        friend class WorldStore;
    };
    static std::unique_ptr<WorldStore> create(const WorldState &initial,
                                             const ProviderRegistry &registry,
                                             QString *error = nullptr,
                                             std::shared_ptr<GameTimeline> timeline = {});
    static std::unique_ptr<WorldStore> create(const WorldState &initial,
                                             const ProviderRegistry &registry,
                                             const CheckpointRetention &retention,
                                             QString *error = nullptr,
                                             std::shared_ptr<GameTimeline> timeline = {});
    const WorldState &state() const { return *m_state; }
    std::shared_ptr<const WorldState> capture() const { return m_state; }
    std::shared_ptr<const WorldState> checkpointState(const QString &anchorId) const;
    GameTimeline &timeline() { return *m_timeline; }
    const GameTimeline &timeline() const { return *m_timeline; }
    quint64 revision() const { return m_revision; }
    // Managed skills can mutate shared typed state through a detached transaction.
    bool update(const std::function<bool(WorldState &, QString *)> &mutation,
                QString *error = nullptr);
    GameTimeline::Anchor checkpoint(GameTimeline::AnchorKind kind,
                                    const QString &scopeId, const QString &playerId = {},
                                    QString *error = nullptr);
    // Import authoritative native values and bind their checkpoint in one commit.
    // At a real normal-round boundary, alsoRoundStart binds FullRound and
    // PlayerTurn to the same immutable state in that single commit.
    GameTimeline::Anchor checkpointUpdate(const std::function<bool(WorldState &, QString *)> &mutation,
                                          GameTimeline::AnchorKind kind, const QString &scopeId,
                                          const QString &playerId = {}, QString *error = nullptr,
                                          bool alsoRoundStart = false);
    std::unique_ptr<Candidate> prepareRestore(const QString &anchorId,
                                              QString *error = nullptr) const;
    // All fallible validation/allocation precedes these swaps. No callbacks here.
    // Observers (statistics/clients) are notified by the caller AFTER success.
    bool publish(Candidate &&candidate, QString *error = nullptr);
    bool canPublish(const Candidate &candidate, QString *error = nullptr) const;
private:
    WorldStore(const WorldState &initial, const ProviderRegistry &registry,
               const CheckpointRetention &retention,
               std::shared_ptr<GameTimeline> timeline);
    bool prepareValues(WorldState &candidate, QString *error) const;
    void retainCheckpoints(GameTimeline &timeline,
                           QMap<QString, std::shared_ptr<const WorldState>> &checkpoints) const;
    std::shared_ptr<const WorldState> m_state;
    ProviderRegistry m_registry;
    // Room-managed worlds share the Room's one authoritative timeline object.
    // Standalone core stores allocate an independent timeline at create().
    std::shared_ptr<GameTimeline> m_timeline;
    CheckpointRetention m_checkpointRetention;
    quint64 m_revision = 0;
    QMap<QString, std::shared_ptr<const WorldState>> m_checkpoints;
};

} // namespace GameState
#endif

#ifndef QSAN_ROOM_MANAGED_STATE_H
#define QSAN_ROOM_MANAGED_STATE_H

#include "game-state-contract.h"
#include <QPointer>
#include <memory>

class Room;
class LuaRuntime;
class ServerPlayer;

// Native setup and explicitly audited normal-turn adapters. Dynamic topology,
// unmanaged callbacks and suspended/extra-turn continuations are unsupported.
class RoomManagedState
{
public:
    explicit RoomManagedState(Room &room);
    ~RoomManagedState();
    RoomManagedState(const RoomManagedState &) = delete;
    RoomManagedState &operator=(const RoomManagedState &) = delete;

    class Candidate {
    public:
        ~Candidate();
        Candidate(Candidate &&) noexcept;
        Candidate &operator=(Candidate &&) noexcept;
        Candidate(const Candidate &) = delete;
        Candidate &operator=(const Candidate &) = delete;
    private:
        struct Data;
        explicit Candidate(std::unique_ptr<Data> data);
        std::unique_ptr<Data> d;
        friend class RoomManagedState;
    };

    // Setup requires initialized VMs and unloaded rules. Running enrollment is
    // admitted only by RoomThread at an audited normal-turn boundary.
    bool initialize(const GameState::ProviderRegistry &registry, QString *error = nullptr);
    bool initializeRunning(const GameState::ProviderRegistry &registry, QString *error = nullptr);
    bool checkpointTurn(const QString &turnScope, const QString &roundScope, bool beginsRound,
                        QString *error = nullptr);
    bool running() const { return m_running; }
    // Called before shutdown releases tag leases; only enrolled owned clones.
    void releaseOwnedCardsForShutdown();
    bool installLuaProvider(LuaRuntime &runtime, const QString &providerId, QString *error = nullptr);
    const GameState::WorldStore *worldStore() const { return m_store.get(); }
    QString checkpoint(const QString &name, QString *error = nullptr);
    std::unique_ptr<Candidate> prepareRestore(const QString &anchorId, QString *error = nullptr);
    bool publish(Candidate &&candidate, QString *error = nullptr);
    static QString skillId(const QString &playerId, const QString &skillName, int instanceId);

private:
    friend class RoomThread;
    friend class RequestCoordinator;
    struct NativeSlice;
    bool initializeImpl(const GameState::ProviderRegistry &registry, QString *error);
    QVariantMap runningWire(const NativeSlice &native) const;
    bool restoreRunningWire(NativeSlice &native, const QVariantMap &wire, QString *error) const;
    void publishRunning(NativeSlice &native) noexcept;
    bool quiescent(QString *error) const;
    std::unique_ptr<NativeSlice> captureNative(QString *error) const;
    QByteArray nativeFingerprint(const NativeSlice &native) const;
    bool project(const NativeSlice &native, GameState::WorldState &world, QString *error) const;
    bool buildNativeCandidate(NativeSlice &native, const GameState::WorldState &world, QString *error) const;
    Room &m_room;
    GameState::ProviderRegistry m_registry;
    std::unique_ptr<GameState::WorldStore> m_store;
    QByteArray m_fixedTopology;
    QVector<QPointer<ServerPlayer>> m_enrolledPlayers;
    bool m_running = false;
    QHash<int, QPointer<QObject>> m_enrolledCards;
    QHash<int, QPointer<QObject>> m_inactiveCards;
};

#endif

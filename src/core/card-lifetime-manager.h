#ifndef QSANGUOSHA_CARD_LIFETIME_MANAGER_H
#define QSANGUOSHA_CARD_LIFETIME_MANAGER_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <QVariant>
#include <QtGlobal>

#include <atomic>
#include <memory>
#include <initializer_list>

class Card;
class CardLifetimeManager;
struct lua_State;

struct CardLifetimeRuntimeContext {
    const void *domain = nullptr;
    const void *identity = nullptr;
    quint64 generation = 0;
    lua_State *state = nullptr;
};

struct CardTagOwner {
    Card *card = nullptr;
};

Q_DECLARE_METATYPE(CardTagOwner)

enum class CardLifetimeMode : quint8 {
    ObserveOnly,
    ManagedReclaim
};

enum class CardLifetimeState : quint8 {
    ObservedExternal,
    ObservedDefinition,
    PendingDelete,
    Adopted,
    Retired,
    Dead
};

CardLifetimeMode defaultCardLifetimeMode();
CardLifetimeManager &globalCardLifetimeManager();

struct CardLifetimeGauge {
    quint64 clone_created = 0;
    quint64 factory_unclaimed = 0;
    quint64 unknown_unclaimed = 0;
    quint64 native_delete_requested = 0;
    quint64 lua_delete_requested = 0;
    quint64 pending_delete = 0;
    quint64 retired = 0;
    quint64 actually_destroyed = 0;
    quint64 stale_access = 0;
    quint64 double_delete_request = 0;
    quint64 unknown_card_delete = 0;
    quint64 unknown_qvariant_card_payload = 0;
    quint64 adopted_delete_ignored = 0;
    quint64 definition_delete_ignored = 0;
    quint64 adopted_after_delete_request = 0;
    quint64 adoption_reserved = 0;
    quint64 adoption_failed = 0;
    quint64 affinity_transfer_failed = 0;
    quint64 managed_live = 0;
    quint64 wrapper_leases = 0;
    quint64 native_leases = 0;
    quint64 lua_pins = 0;
    quint64 sidecar_edges = 0;
    quint64 blocked_by_legacy_change_list = 0;
    quint64 change_list_self_cycle = 0;
    quint64 change_list_cycles = 0;
    quint64 change_list_reuse_reconnect = 0;
    quint64 external_direct_destroy = 0;
    quint64 unapproved_card_raw_delete = 0;
    quint64 card_delete_bypass = 0;
    quint64 peak_managed_cards = 0;
    quint64 peak_pending_delete = 0;
    quint64 peak_wrapper_count = 0;
};

struct CardLifetimeMutexProfile {
    bool enabled = false;
    quint64 lock_count = 0;
    quint64 contended_count = 0;
    qint64 wait_ns = 0;
    qint64 max_wait_ns = 0;
};

struct CardLifetimeToken {
    const void *address = nullptr;
    quint64 generation = 0;
    bool originalOwner = false;
    bool live = true;
    CardLifetimeState state = CardLifetimeState::ObservedExternal;
};

class CardLifetimeManager final
{
public:
    explicit CardLifetimeManager(CardLifetimeMode mode = defaultCardLifetimeMode(),
                                 QThread *ownerThread = nullptr,
                                 bool enableMutexProfile = false);
    ~CardLifetimeManager();

    CardLifetimeManager(const CardLifetimeManager &) = delete;
    CardLifetimeManager &operator=(const CardLifetimeManager &) = delete;

    CardLifetimeMode mode() const;
    QThread *ownerThread() const;

    std::shared_ptr<const CardLifetimeToken> observeLive(const void *card,
                                                         bool originalOwner = false);
    std::shared_ptr<const CardLifetimeToken> observeCard(Card *card,
                                                         bool originalOwner = false);
    std::shared_ptr<const CardLifetimeToken> recordFactoryClone(Card *card);
    void recordOwningFactoryResult(const std::shared_ptr<const CardLifetimeToken> &token);
    void recordDeferredDelete(Card *card);
    std::shared_ptr<const CardLifetimeToken> liveToken(const void *card) const;
    QThread *affinityThread(const std::shared_ptr<const CardLifetimeToken> &token) const;
    bool isBaselineToken(const void *domain,
                         const std::shared_ptr<const CardLifetimeToken> &token) const;
    bool requestNativeDelete(Card *card);
    void notifyDestroyed(Card *card);
    static void notifyDestroyedCard(Card *card);
    static CardLifetimeManager *enterLuaPinForCurrentThread();
    static void leaveLuaPinForCurrentThread(CardLifetimeManager *manager);
    static const void *setCurrentDomain(const void *domain);
    static CardLifetimeRuntimeContext setCurrentRuntimeContext(
        const void *domain, const void *identity, quint64 generation, lua_State *state);
    bool invalidateIfObserved(const void *card);
    bool invalidateIfObserved(const std::shared_ptr<const CardLifetimeToken> &token);
    static bool invalidateObservedCard(const void *card);
    bool isLive(const void *card) const;
    bool isLive(const std::shared_ptr<const CardLifetimeToken> &token) const;
    CardLifetimeState state(const std::shared_ptr<const CardLifetimeToken> &token) const;

    bool requestNativeDelete(const std::shared_ptr<const CardLifetimeToken> &token);
    bool markAdopted(const std::shared_ptr<const CardLifetimeToken> &token);
    bool reserveAdoption(const std::shared_ptr<const CardLifetimeToken> &token);
    void cancelAdoption(const std::shared_ptr<const CardLifetimeToken> &token, bool transferFailed = false);
    bool requestLuaDelete(const void *card, QByteArray *error = nullptr);
    bool requestLuaDelete(const std::shared_ptr<const CardLifetimeToken> &token,
                          QByteArray *error = nullptr);
    bool rejectOpaqueVariant(QByteArray *error = nullptr);
    bool retainWrapper(const std::shared_ptr<const CardLifetimeToken> &token);
    bool releaseWrapper(const std::shared_ptr<const CardLifetimeToken> &token);
    bool retainNativeLease(const std::shared_ptr<const CardLifetimeToken> &token);
    bool releaseNativeLease(const std::shared_ptr<const CardLifetimeToken> &token);
    bool addChangeEdge(Card *source, const Card *target);
    void removeChangeEdges(const Card *card);
    bool bindTag(const void *container, const QByteArray &key, Card *card);
    void releaseTag(const void *container, const QByteArray &key);
    void releaseTags(const void *container);
    void retainEventPayload(const void *owner,
                            std::initializer_list<const Card *> cards);
    void releaseEventPayload(const void *owner);
    quint64 releaseEventPayloads(const void *domain);
    bool retainVariantPayload(const void *owner, const QVariant &value, QByteArray *error = nullptr);
    bool retainVariantTag(const void *container, const QByteArray &key, const QVariant &value,
                          QByteArray *error = nullptr);
    void releaseVariantTag(const void *container, const QByteArray &key);
    void releaseVariantTags(const void *container);
    bool bindWrapper(const void *wrapper,
                     const std::shared_ptr<const CardLifetimeToken> &token,
                     bool originalOwner = false);
    std::shared_ptr<const CardLifetimeToken> wrapperBinding(const void *wrapper,
                                                            bool *originalOwner = nullptr) const;
    std::shared_ptr<const CardLifetimeToken> releaseWrapperBinding(const void *wrapper,
                                                                    bool *originalOwner = nullptr);
    quint64 releaseWrapperBindings(const void *domain);
    quint64 releaseWrapperBindings(const void *domain, const void *identity,
                                   quint64 generation, lua_State *state);
    quint64 drain();
    quint64 drainDomain(const void *domain,
                        QList<QPointer<QObject>> *retiredObjects = nullptr);
    bool finalizeWorkerDomain(const void *domain, quint64 *retired = nullptr);

    void registerRuntimeDomain(const void *domain, const void *identity,
                               quint64 generation, lua_State *state = nullptr);
    void unregisterRuntimeDomain(const void *domain, const void *identity,
                                 quint64 generation, lua_State *state = nullptr);
    CardLifetimeGauge gaugeForRuntime(const void *domain, const void *identity,
                                      quint64 generation, lua_State *state = nullptr) const;

    CardLifetimeGauge gauge() const;
    CardLifetimeGauge gaugeForDomain(const void *domain) const;
    CardLifetimeMutexProfile mutexProfile() const;
    void dumpDomain(const void *domain) const;
    quint64 entryCount() const;
    QSet<const void *> entryAddresses() const;
    QSet<const void *> entryAddressesForDomain(const void *domain) const;
    void setDomainBaseline(const void *domain, const QSet<const void *> &addresses);
    void unregisterDomainBaseline(const void *domain);
    quint64 entryCountForDomain(const void *domain) const;
    quint64 activeScopeDepth() const;
    quint64 activeScopeDepthForDomain(const void *domain) const;
    void enterScope();
    void leaveScope();
    void enterLuaPin();
    void leaveLuaPin();
    quint64 luaPinDepth() const;
    bool resetForTest();

    static const char *staleCardError();
    static const char *unknownOwnershipError();
    static const char *opaqueVariantError();

private:
    class ProfiledMutex final
    {
    public:
        explicit ProfiledMutex(bool enabled = false);
        void lock();
        void unlock() noexcept;
        CardLifetimeMutexProfile profile() const;

    private:
        QMutex m_mutex;
        const bool m_enabled;
        std::atomic<quint64> m_lockCount{0};
        std::atomic<quint64> m_contendedCount{0};
        std::atomic<qint64> m_waitNs{0};
        std::atomic<qint64> m_maxWaitNs{0};
    };

    struct Entry {
        std::shared_ptr<CardLifetimeToken> token;
        quint64 wrappers = 0;
        quint64 nativeLeases = 0;
        bool pending = false;
        bool nativeDelete = false;
        bool destructorWon = false;
        quint64 adoptionReservations = 0;
        QPointer<QObject> object;
        QThread *affinityThread = nullptr;
        bool physical = false;
        bool cloneRecorded = false;
        bool factoryUnclaimed = false;
        bool unknownUnclaimed = false;
        bool deleteBypass = false;
        bool destructionClassified = false;
        const void *domain = nullptr;
        const void *runtimeIdentity = nullptr;
        quint64 runtimeGeneration = 0;
        lua_State *runtimeState = nullptr;
        const void *baselineDomain = nullptr;
    };
    struct WrapperBinding {
        std::shared_ptr<const CardLifetimeToken> token;
        bool originalOwner = false;
        const void *domain = nullptr;
        const void *runtimeIdentity = nullptr;
        quint64 runtimeGeneration = 0;
        lua_State *runtimeState = nullptr;
    };
    struct ChangeEdge {
        const void *source = nullptr;
        std::shared_ptr<const CardLifetimeToken> sourceToken;
        const void *target = nullptr;
        std::shared_ptr<const CardLifetimeToken> targetToken;
    };
    struct TagBinding {
        const void *container = nullptr;
        QByteArray key;
        std::shared_ptr<const CardLifetimeToken> token;
    };

    Entry *entryFor(const std::shared_ptr<const CardLifetimeToken> &token);
    const Entry *entryFor(const std::shared_ptr<const CardLifetimeToken> &token) const;
    Entry *entryForLease(const std::shared_ptr<const CardLifetimeToken> &token);
    bool invalidateIfObservedLocked(const void *card,
                                    const CardLifetimeToken *expectedToken);
    void clearUnclaimedLocked(Entry &entry);
    void classifyPhysicalDestructionLocked(Entry &entry);
    void reconcileDestroyedLocked();
    void reapDeadLocked(const std::shared_ptr<const CardLifetimeToken> &token);
    quint64 drainImpl(const void *domain,
                      QList<QPointer<QObject>> *retiredObjects);
    void updatePeaksLocked();

    mutable ProfiledMutex m_mutex;
    QHash<const void *, Entry> m_entries;
    QHash<const CardLifetimeToken *, Entry> m_deadEntries;
    QHash<const void *, WrapperBinding> m_wrappers;
    QList<ChangeEdge> m_changeEdges;
    QList<TagBinding> m_tagBindings;
    QHash<const void *, QList<std::shared_ptr<const CardLifetimeToken>>> m_eventLeases;
    QHash<const void *, QHash<QByteArray, QList<std::shared_ptr<const CardLifetimeToken>>>> m_variantTags;
    CardLifetimeMode m_mode;
    QThread *m_ownerThread;
    quint64 m_nextGeneration = 1;
    quint64 m_activeScopes = 0;
    quint64 m_luaPins = 0;
    QHash<const void *, quint64> m_domainActiveScopes;
    QHash<const void *, quint64> m_domainLuaPins;
    QHash<const void *, QHash<quint64, quint64>> m_runtimeLuaPins;
    QHash<const void *, QHash<const void *, quint64>> m_domainBaselines;
    struct RuntimeRegistration {
        const void *domain = nullptr;
        const void *identity = nullptr;
        quint64 generation = 0;
        lua_State *state = nullptr;
        CardLifetimeGauge baseline;
    };
    QList<RuntimeRegistration> m_runtimeRegistrations;
    CardLifetimeGauge m_gauge;
};

class CardLifetimeScope final
{
public:
    explicit CardLifetimeScope(CardLifetimeManager &manager) : m_manager(&manager) { m_manager->enterScope(); }
    ~CardLifetimeScope() { if (m_manager) m_manager->leaveScope(); }
    CardLifetimeScope(const CardLifetimeScope &) = delete;
    CardLifetimeScope &operator=(const CardLifetimeScope &) = delete;
private:
    CardLifetimeManager *m_manager;
};

class CardLifetimeLease final
{
public:
    CardLifetimeLease() = default;
    CardLifetimeLease(CardLifetimeManager &manager,
                      std::shared_ptr<const CardLifetimeToken> token);
    ~CardLifetimeLease();
    CardLifetimeLease(const CardLifetimeLease &) = delete;
    CardLifetimeLease &operator=(const CardLifetimeLease &) = delete;
    CardLifetimeLease(CardLifetimeLease &&other) noexcept;
    CardLifetimeLease &operator=(CardLifetimeLease &&other) noexcept;
    bool isValid() const { return m_token != nullptr; }
private:
    CardLifetimeManager *m_manager = nullptr;
    std::shared_ptr<const CardLifetimeToken> m_token;
};

#endif

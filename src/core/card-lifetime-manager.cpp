#include "card-lifetime-manager.h"
#include "card.h"
#include "skill.h"
#include "structs.h"

#include <QMutexLocker>
#include <QSet>

#include <cstdio>

namespace {
QMutex associationMutex;
QHash<const void *, CardLifetimeManager *> cardAssociations;
QSet<CardLifetimeManager *> managers;
thread_local const void *currentDomain = nullptr;
thread_local const void *currentRuntimeIdentity = nullptr;
thread_local quint64 currentRuntimeGeneration = 0;
thread_local lua_State *currentRuntimeState = nullptr;

void unregisterManager(CardLifetimeManager *manager)
{
    QMutexLocker lock(&associationMutex);
    for (auto it = cardAssociations.begin(); it != cardAssociations.end();) {
        if (it.value() == manager)
            it = cardAssociations.erase(it);
        else
            ++it;
    }
}
}

CardLifetimeMode defaultCardLifetimeMode()
{
    return CardLifetimeMode::ManagedReclaim;
}

CardLifetimeManager &globalCardLifetimeManager()
{
    static CardLifetimeManager manager;
    return manager;
}

CardLifetimeManager::CardLifetimeManager(CardLifetimeMode mode, QThread *ownerThread)
    : m_mode(mode), m_ownerThread(ownerThread ? ownerThread : QThread::currentThread())
{
    QMutexLocker lock(&associationMutex);
    managers.insert(this);
}

CardLifetimeManager::~CardLifetimeManager()
{
    unregisterManager(this);
    {
        QMutexLocker lock(&associationMutex);
        managers.remove(this);
    }
    QMutexLocker lock(&m_mutex);
    m_wrappers.clear();
    m_changeEdges.clear();
    m_tagBindings.clear();
    m_eventLeases.clear();
    m_variantTags.clear();
    m_entries.clear();
    m_deadEntries.clear();
}

CardLifetimeMode CardLifetimeManager::mode() const { return m_mode; }
QThread *CardLifetimeManager::ownerThread() const { return m_ownerThread; }

std::shared_ptr<const CardLifetimeToken> CardLifetimeManager::observeLive(const void *card,
                                                                            bool originalOwner)
{
    if (!card)
        return {};
    QMutexLocker lock(&m_mutex);
    auto found = m_entries.find(card);
    if (found != m_entries.end()) {
        if (found->token->live)
            return found->token;
        if (found->destructorWon) {
            const auto staleToken = found->token;
            Entry staleEntry = std::move(found.value());
            m_entries.erase(found);
            m_deadEntries.insert(staleToken.get(), std::move(staleEntry));
            reapDeadLocked(staleToken);
        } else if (found->physical) {
            ++m_gauge.stale_access;
            return {};
        } else {
            ++m_gauge.stale_access;
            m_entries.erase(found);
        }
    }
    Entry entry;
    entry.token = std::make_shared<CardLifetimeToken>();
    entry.token->address = card;
    entry.token->generation = m_nextGeneration++;
    entry.token->originalOwner = originalOwner;
    entry.token->state = originalOwner ? CardLifetimeState::ObservedDefinition
                                       : CardLifetimeState::ObservedExternal;
    entry.domain = currentDomain;
    entry.runtimeIdentity = currentRuntimeIdentity;
    entry.runtimeGeneration = currentRuntimeGeneration;
    entry.runtimeState = currentRuntimeState;
    if (currentDomain) {
        for (auto baseline = m_domainBaselines.cbegin();
             baseline != m_domainBaselines.cend(); ++baseline) {
            const quint64 baselineGeneration = baseline.value().value(card, 0);
            if (baselineGeneration != 0 && baselineGeneration == entry.token->generation) {
                entry.baselineDomain = baseline.key();
                break;
            }
        }
    }
    m_entries.insert(card, std::move(entry));
    ++m_gauge.managed_live;
    updatePeaksLocked();
    return m_entries.find(card)->token;
}

bool CardLifetimeManager::requestNativeDelete(Card *card)
{
    if (!card)
        return false;
    return requestNativeDelete(observeCard(card));
}

void CardLifetimeManager::notifyDestroyedCard(Card *card)
{
    if (!card)
        return;
    QList<CardLifetimeManager *> snapshot;
    {
        QMutexLocker lock(&associationMutex);
        snapshot = managers.values();
    }
    for (CardLifetimeManager *manager : snapshot)
        if (manager)
            manager->notifyDestroyed(card);
}

CardLifetimeManager *CardLifetimeManager::enterLuaPinForCurrentThread()
{
    QList<CardLifetimeManager *> snapshot;
    {
        QMutexLocker lock(&associationMutex);
        snapshot = managers.values();
    }
    for (CardLifetimeManager *manager : snapshot) {
        if (!manager)
            continue;
        bool matches = false;
        {
            QMutexLocker lock(&manager->m_mutex);
            for (const RuntimeRegistration &registration : std::as_const(
                     manager->m_runtimeRegistrations)) {
                if (registration.domain == currentDomain
                    && registration.identity == currentRuntimeIdentity
                    && registration.generation == currentRuntimeGeneration
                    && registration.state == currentRuntimeState) {
                    matches = true;
                    break;
                }
            }
        }
        if (matches) {
            manager->enterLuaPin();
            return manager;
        }
    }
    return nullptr;
}

void CardLifetimeManager::leaveLuaPinForCurrentThread(CardLifetimeManager *manager)
{
    if (manager)
        manager->leaveLuaPin();
}

const void *CardLifetimeManager::setCurrentDomain(const void *domain)
{
    const void *previous = currentDomain;
    currentDomain = domain;
    return previous;
}

CardLifetimeRuntimeContext CardLifetimeManager::setCurrentRuntimeContext(
    const void *domain, const void *identity, quint64 generation, lua_State *state)
{
    CardLifetimeRuntimeContext previous;
    previous.domain = currentDomain;
    previous.identity = currentRuntimeIdentity;
    previous.generation = currentRuntimeGeneration;
    previous.state = currentRuntimeState;
    currentDomain = domain;
    currentRuntimeIdentity = identity;
    currentRuntimeGeneration = generation;
    currentRuntimeState = state;
    return previous;
}

std::shared_ptr<const CardLifetimeToken> CardLifetimeManager::observeCard(Card *card,
                                                                          bool originalOwner)
{
    if (!card)
        return {};
    const QThread *affinity = card->thread();
    const auto token = observeLive(card, originalOwner);
    if (!token)
        return {};
    bool connectDestroyed = false;
    {
        QMutexLocker lock(&m_mutex);
        const auto found = m_entries.find(card);
        if (found != m_entries.end() && found->token.get() == token.get()) {
            if (originalOwner && found->token->state == CardLifetimeState::ObservedExternal) {
                found->token->originalOwner = true;
                found->token->state = CardLifetimeState::ObservedDefinition;
                clearUnclaimedLocked(found.value());
            }
            found->object = card;
            found->affinityThread = const_cast<QThread *>(affinity);
            if (!found->physical) {
                found->physical = true;
                connectDestroyed = true;
            }
        }
    }
    {
        QMutexLocker associationLock(&associationMutex);
        cardAssociations.insert(card, this);
    }
    if (connectDestroyed)
        QObject::connect(card, &QObject::destroyed, [card] {
            CardLifetimeManager::notifyDestroyedCard(card);
        });
    return token;
}

std::shared_ptr<const CardLifetimeToken> CardLifetimeManager::recordFactoryClone(Card *card)
{
    const auto token = observeCard(card);
    if (!token)
        return {};
    QMutexLocker lock(&m_mutex);
    Entry *entry = entryFor(token);
    if (!entry)
        return {};
    if (!entry->cloneRecorded) {
        entry->cloneRecorded = true;
        ++m_gauge.clone_created;
    }
    if (entry->unknownUnclaimed) {
        entry->unknownUnclaimed = false;
        if (m_gauge.unknown_unclaimed > 0)
            --m_gauge.unknown_unclaimed;
    }
    if (!entry->factoryUnclaimed) {
        entry->factoryUnclaimed = true;
        ++m_gauge.factory_unclaimed;
    }
    return token;
}

void CardLifetimeManager::recordOwningFactoryResult(
    const std::shared_ptr<const CardLifetimeToken> &token)
{
    QMutexLocker lock(&m_mutex);
    Entry *entry = entryFor(token);
    if (!entry || entry->factoryUnclaimed || entry->unknownUnclaimed)
        return;
    entry->unknownUnclaimed = true;
    ++m_gauge.unknown_unclaimed;
}

void CardLifetimeManager::recordDeferredDelete(Card *card)
{
    if (!card)
        return;
    {
        QMutexLocker lock(&m_mutex);
        auto found = m_entries.find(card);
        if (found != m_entries.end()) {
            if (!found->token->live || found->pending || found->nativeDelete
                || found->deleteBypass)
                return;
            found->deleteBypass = true;
            ++m_gauge.card_delete_bypass;
            return;
        }
    }
    const auto token = observeCard(card, card->parent() != nullptr);
    QMutexLocker lock(&m_mutex);
    Entry *entry = entryFor(token);
    if (!entry || entry->pending || entry->nativeDelete || entry->deleteBypass)
        return;
    entry->deleteBypass = true;
    ++m_gauge.card_delete_bypass;
}

std::shared_ptr<const CardLifetimeToken> CardLifetimeManager::liveToken(const void *card) const
{
    QMutexLocker lock(&m_mutex);
    const auto found = m_entries.constFind(card);
    if (found == m_entries.cend() || !found->token->live)
        return {};
    return found->token;
}

QThread *CardLifetimeManager::affinityThread(
    const std::shared_ptr<const CardLifetimeToken> &token) const
{
    if (!token)
        return nullptr;
    QMutexLocker lock(&m_mutex);
    const auto found = m_entries.constFind(token->address);
    if (found == m_entries.cend() || found->token.get() != token.get()
        || found->token->generation != token->generation || !found->token->live)
        return nullptr;
    return found->affinityThread;
}

bool CardLifetimeManager::isBaselineToken(
    const void *domain, const std::shared_ptr<const CardLifetimeToken> &token) const
{
    if (!domain || !token)
        return false;
    QMutexLocker lock(&m_mutex);
    const auto found = m_entries.constFind(token->address);
    return found != m_entries.cend() && found->token.get() == token.get()
        && found->token->generation == token->generation
        && found->baselineDomain == domain;
}

bool CardLifetimeManager::invalidateObservedCard(const void *card)
{
    if (!card)
        return false;
    CardLifetimeManager *manager = nullptr;
    {
        QMutexLocker lock(&associationMutex);
        manager = cardAssociations.value(card, nullptr);
    }
    if (!manager)
        return false;
    const bool invalidated = manager->invalidateIfObserved(card);
    {
        QMutexLocker lock(&associationMutex);
        if (cardAssociations.value(card, nullptr) == manager)
            cardAssociations.remove(card);
    }
    return invalidated;
}

CardLifetimeManager::Entry *CardLifetimeManager::entryFor(
    const std::shared_ptr<const CardLifetimeToken> &token)
{
    if (!token)
        return nullptr;
    auto found = m_entries.find(token->address);
    if (found == m_entries.end() || found->token.get() != token.get() ||
        !found->token->live || found->token->generation != token->generation)
        return nullptr;
    return &found.value();
}

CardLifetimeManager::Entry *CardLifetimeManager::entryForLease(
    const std::shared_ptr<const CardLifetimeToken> &token)
{
    if (!token)
        return nullptr;
    auto found = m_entries.find(token->address);
    if (found != m_entries.end() && found->token.get() == token.get()
        && found->token->generation == token->generation)
        return &found.value();
    auto stale = m_deadEntries.find(token.get());
    if (stale == m_deadEntries.end() || stale->token->generation != token->generation)
        return nullptr;
    return &stale.value();
}

void CardLifetimeManager::reapDeadLocked(
    const std::shared_ptr<const CardLifetimeToken> &token)
{
    Entry *entry = entryForLease(token);
    if (!entry || entry->token->live
        || entry->wrappers != 0 || entry->nativeLeases != 0
        || entry->adoptionReservations != 0 || entry->object != nullptr)
        return;
    const void *address = token->address;
    const bool referenced = std::any_of(m_wrappers.cbegin(), m_wrappers.cend(),
        [&](const WrapperBinding &binding) { return binding.token.get() == token.get(); })
        || std::any_of(m_changeEdges.cbegin(), m_changeEdges.cend(),
        [&](const ChangeEdge &edge) { return edge.sourceToken.get() == token.get()
            || edge.targetToken.get() == token.get(); })
        || std::any_of(m_tagBindings.cbegin(), m_tagBindings.cend(),
        [&](const TagBinding &binding) { return binding.token.get() == token.get(); });
    if (referenced)
        return;
    for (auto it = m_eventLeases.cbegin(); it != m_eventLeases.cend(); ++it)
        for (const auto &held : it.value())
            if (held.get() == token.get()) return;
    for (auto outer = m_variantTags.cbegin(); outer != m_variantTags.cend(); ++outer)
        for (auto inner = outer.value().cbegin(); inner != outer.value().cend(); ++inner)
            for (const auto &held : inner.value())
                if (held.get() == token.get()) return;
    auto active = m_entries.find(address);
    if (active != m_entries.end() && active->token.get() == token.get())
        m_entries.erase(active);
    else
        m_deadEntries.remove(token.get());
}

void CardLifetimeManager::clearUnclaimedLocked(Entry &entry)
{
    if (entry.factoryUnclaimed) {
        entry.factoryUnclaimed = false;
        if (m_gauge.factory_unclaimed > 0)
            --m_gauge.factory_unclaimed;
    }
    if (entry.unknownUnclaimed) {
        entry.unknownUnclaimed = false;
        if (m_gauge.unknown_unclaimed > 0)
            --m_gauge.unknown_unclaimed;
    }
}

void CardLifetimeManager::classifyPhysicalDestructionLocked(Entry &entry)
{
    if (entry.destructionClassified)
        return;
    entry.destructionClassified = true;
    const bool managedRequest = entry.pending || entry.nativeDelete || entry.deleteBypass;
    const bool approvedOwner = entry.token->originalOwner
        || entry.token->state == CardLifetimeState::Adopted;
    if (!managedRequest
        && (entry.wrappers > 0 || entry.nativeLeases > 0
            || entry.adoptionReservations > 0))
        ++m_gauge.external_direct_destroy;
    if (!managedRequest && !approvedOwner)
        ++m_gauge.unapproved_card_raw_delete;
    clearUnclaimedLocked(entry);
}

const CardLifetimeManager::Entry *CardLifetimeManager::entryFor(
    const std::shared_ptr<const CardLifetimeToken> &token) const
{
    if (!token)
        return nullptr;
    auto found = m_entries.constFind(token->address);
    if (found == m_entries.cend() || found->token.get() != token.get() ||
        !found->token->live || found->token->generation != token->generation)
        return nullptr;
    return &found.value();
}

bool CardLifetimeManager::invalidateIfObservedLocked(
    const void *card, const CardLifetimeToken *expectedToken)
{
    auto found = m_entries.find(card);
    if (found == m_entries.end())
        return false;
    if (expectedToken && (found->token.get() != expectedToken
                          || found->token->generation != expectedToken->generation))
        return false;
    if (!found->token->live) {
        if (found->token->state == CardLifetimeState::Retired)
            found->token->state = CardLifetimeState::Dead;
        return false;
    }
    if (found->physical)
        classifyPhysicalDestructionLocked(found.value());
    found->token->live = false;
    found->token->state = CardLifetimeState::Dead;
    if (found->pending) {
        found->pending = false;
        if (m_gauge.pending_delete > 0)
            --m_gauge.pending_delete;
    }
    if (!found->pending)
        ++m_gauge.retired;
    if (m_gauge.managed_live > 0)
        --m_gauge.managed_live;
    if (found->baselineDomain) {
        auto baseline = m_domainBaselines.find(found->baselineDomain);
        if (baseline != m_domainBaselines.end()
            && baseline->value(card, 0) == found->token->generation) {
            baseline->remove(card);
            if (baseline->isEmpty())
                m_domainBaselines.erase(baseline);
        }
    }
    reapDeadLocked(found->token);
    return true;
}

bool CardLifetimeManager::invalidateIfObserved(const void *card)
{
    bool invalidated = false;
    {
        QMutexLocker lock(&m_mutex);
        invalidated = invalidateIfObservedLocked(card, nullptr);
    }
    if (!invalidated)
        return false;
    {
        QMutexLocker associationLock(&associationMutex);
        if (cardAssociations.value(card, nullptr) == this)
            cardAssociations.remove(card);
    }
    return true;
}

bool CardLifetimeManager::invalidateIfObserved(
    const std::shared_ptr<const CardLifetimeToken> &token)
{
    if (!token)
        return false;
    bool invalidated = false;
    {
        QMutexLocker lock(&m_mutex);
        invalidated = invalidateIfObservedLocked(token->address, token.get());
    }
    if (!invalidated)
        return false;
    {
        QMutexLocker associationLock(&associationMutex);
        if (cardAssociations.value(token->address, nullptr) == this)
            cardAssociations.remove(token->address);
    }
    return true;
}

void CardLifetimeManager::notifyDestroyed(Card *card)
{
    if (!card)
        return;
    bool removeAssociation = false;
    {
        QMutexLocker lock(&m_mutex);
        auto found = m_entries.find(card);
        if (found == m_entries.end())
            return;
        if (!found->destructorWon) {
            classifyPhysicalDestructionLocked(found.value());
            found->destructorWon = true;
            found->object = nullptr;
            found->token->live = false;
            found->token->state = CardLifetimeState::Dead;
            if (found->pending) {
                found->pending = false;
                if (m_gauge.pending_delete > 0)
                    --m_gauge.pending_delete;
            }
            if (m_gauge.managed_live > 0)
                --m_gauge.managed_live;
            ++m_gauge.actually_destroyed;
        }
        if (found->baselineDomain) {
            auto baseline = m_domainBaselines.find(found->baselineDomain);
            if (baseline != m_domainBaselines.end()
                && baseline->value(card, 0) == found->token->generation) {
                baseline->remove(card);
                if (baseline->isEmpty())
                    m_domainBaselines.erase(baseline);
            }
        }
        removeAssociation = found->wrappers == 0 && found->nativeLeases == 0
            && found->adoptionReservations == 0;
        if (removeAssociation)
            m_entries.erase(found);
    }
    if (removeAssociation) {
        QMutexLocker associationLock(&associationMutex);
        if (cardAssociations.value(card, nullptr) == this)
            cardAssociations.remove(card);
    }
}

void CardLifetimeManager::reconcileDestroyedLocked()
{
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (!it->physical || it->object || it->destructorWon) {
            ++it;
            continue;
        }
        const auto token = it->token;
        classifyPhysicalDestructionLocked(it.value());
        it->destructorWon = true;
        it->token->live = false;
        it->token->state = CardLifetimeState::Dead;
        if (it->pending) {
            it->pending = false;
            if (m_gauge.pending_delete > 0)
                --m_gauge.pending_delete;
        }
        if (m_gauge.managed_live > 0)
            --m_gauge.managed_live;
        ++m_gauge.actually_destroyed;
        if (it->baselineDomain) {
            auto baseline = m_domainBaselines.find(it->baselineDomain);
            if (baseline != m_domainBaselines.end()
                && baseline->value(token->address, 0) == token->generation) {
                baseline->remove(token->address);
                if (baseline->isEmpty())
                    m_domainBaselines.erase(baseline);
            }
        }
        reapDeadLocked(token);
        it = m_entries.begin();
    }
}

bool CardLifetimeManager::isLive(const std::shared_ptr<const CardLifetimeToken> &token) const
{
    QMutexLocker lock(&m_mutex);
    return entryFor(token) != nullptr;
}

bool CardLifetimeManager::isLive(const void *card) const
{
    QMutexLocker lock(&m_mutex);
    const auto found = m_entries.constFind(card);
    return found != m_entries.cend() && found->token->live;
}

CardLifetimeState CardLifetimeManager::state(
    const std::shared_ptr<const CardLifetimeToken> &token) const
{
    QMutexLocker lock(&m_mutex);
    const Entry *entry = entryFor(token);
    return entry ? entry->token->state
                 : (token ? token->state : CardLifetimeState::Dead);
}

bool CardLifetimeManager::requestNativeDelete(const std::shared_ptr<const CardLifetimeToken> &token)
{
    QMutexLocker lock(&m_mutex);
    Entry *entry = entryFor(token);
    if (!entry) { ++m_gauge.stale_access; return false; }
    if (entry->token->originalOwner || entry->token->state == CardLifetimeState::Adopted) {
        ++m_gauge.adopted_delete_ignored;
        if (entry->token->state != CardLifetimeState::Adopted)
            ++m_gauge.definition_delete_ignored;
        return true;
    }
    if (entry->pending) { ++m_gauge.double_delete_request; return false; }
    clearUnclaimedLocked(*entry);
    entry->pending = true;
    entry->token->state = CardLifetimeState::PendingDelete;
    ++m_gauge.native_delete_requested;
    ++m_gauge.pending_delete;
    updatePeaksLocked();
    return true;
}

bool CardLifetimeManager::markAdopted(const std::shared_ptr<const CardLifetimeToken> &token)
{
    QMutexLocker lock(&m_mutex);
    Entry *entry = entryFor(token);
    if (!entry)
        return false;
    clearUnclaimedLocked(*entry);
    if (entry->pending) {
        entry->pending = false;
        if (m_gauge.pending_delete > 0)
            --m_gauge.pending_delete;
        ++m_gauge.adopted_after_delete_request;
    }
    entry->token->state = CardLifetimeState::Adopted;
    entry->token->originalOwner = true;
    return true;
}

bool CardLifetimeManager::reserveAdoption(const std::shared_ptr<const CardLifetimeToken> &token)
{
    QMutexLocker lock(&m_mutex);
    Entry *entry = entryFor(token);
    if (!entry || !entry->token->live) {
        ++m_gauge.adoption_failed;
        return false;
    }
    ++entry->adoptionReservations;
    ++m_gauge.adoption_reserved;
    return true;
}

void CardLifetimeManager::cancelAdoption(const std::shared_ptr<const CardLifetimeToken> &token,
                                         bool transferFailed)
{
    QMutexLocker lock(&m_mutex);
    Entry *entry = entryFor(token);
    if (entry && entry->adoptionReservations > 0)
        --entry->adoptionReservations;
    if (m_gauge.adoption_reserved > 0)
        --m_gauge.adoption_reserved;
    if (transferFailed) {
        ++m_gauge.affinity_transfer_failed;
        ++m_gauge.adoption_failed;
    }
}

bool CardLifetimeManager::requestLuaDelete(const std::shared_ptr<const CardLifetimeToken> &token,
                                           QByteArray *error)
{
    QMutexLocker lock(&m_mutex);
    Entry *entry = entryFor(token);
    if (!entry) {
        ++m_gauge.unknown_card_delete;
        if (error) *error = unknownOwnershipError();
        return false;
    }
    if (entry->token->originalOwner || entry->token->state == CardLifetimeState::Adopted) {
        if (entry->token->state == CardLifetimeState::Adopted)
            ++m_gauge.adopted_delete_ignored;
        ++m_gauge.definition_delete_ignored;
        return true;
    }
    if (entry->pending) {
        ++m_gauge.double_delete_request;
        if (error) *error = staleCardError();
        return false;
    }
    clearUnclaimedLocked(*entry);
    entry->pending = true;
    entry->token->state = CardLifetimeState::PendingDelete;
    ++m_gauge.lua_delete_requested;
    ++m_gauge.pending_delete;
    updatePeaksLocked();
    return true;
}

bool CardLifetimeManager::requestLuaDelete(const void *card, QByteArray *error)
{
    QMutexLocker lock(&m_mutex);
    const auto found = m_entries.find(card);
    if (found == m_entries.end() || !found->token->live) {
        ++m_gauge.unknown_card_delete;
        if (error)
            *error = unknownOwnershipError();
        return false;
    }
    Entry *entry = &found.value();
    if (entry->token->originalOwner || entry->token->state == CardLifetimeState::Adopted) {
        ++m_gauge.adopted_delete_ignored;
        if (entry->token->state != CardLifetimeState::Adopted)
            ++m_gauge.definition_delete_ignored;
        return true;
    }
    if (entry->pending)
        return true;
    clearUnclaimedLocked(*entry);
    entry->pending = true;
    entry->token->state = CardLifetimeState::PendingDelete;
    ++m_gauge.lua_delete_requested;
    ++m_gauge.pending_delete;
    updatePeaksLocked();
    return true;
}

bool CardLifetimeManager::rejectOpaqueVariant(QByteArray *error)
{
    QMutexLocker lock(&m_mutex);
    ++m_gauge.unknown_qvariant_card_payload;
    if (error)
        *error = opaqueVariantError();
    return false;
}

bool CardLifetimeManager::retainWrapper(const std::shared_ptr<const CardLifetimeToken> &token)
{
    QMutexLocker lock(&m_mutex);
    Entry *entry = entryFor(token);
    if (!entry) { ++m_gauge.stale_access; return false; }
    ++entry->wrappers;
    ++m_gauge.wrapper_leases;
    updatePeaksLocked();
    return true;
}

bool CardLifetimeManager::releaseWrapper(const std::shared_ptr<const CardLifetimeToken> &token)
{
    QMutexLocker lock(&m_mutex);
    Entry *entry = entryForLease(token);
    if (!entry || entry->wrappers == 0)
        return false;
    --entry->wrappers;
    --m_gauge.wrapper_leases;
    reapDeadLocked(token);
    return true;
}

bool CardLifetimeManager::retainNativeLease(const std::shared_ptr<const CardLifetimeToken> &token)
{
    QMutexLocker lock(&m_mutex);
    Entry *entry = entryFor(token);
    if (!entry) {
        ++m_gauge.stale_access;
        return false;
    }
    clearUnclaimedLocked(*entry);
    ++entry->nativeLeases;
    ++m_gauge.native_leases;
    return true;
}

bool CardLifetimeManager::releaseNativeLease(const std::shared_ptr<const CardLifetimeToken> &token)
{
    QMutexLocker lock(&m_mutex);
    Entry *entry = entryForLease(token);
    if (!entry || entry->nativeLeases == 0)
        return false;
    --entry->nativeLeases;
    if (m_gauge.native_leases > 0)
        --m_gauge.native_leases;
    reapDeadLocked(token);
    return true;
}

bool CardLifetimeManager::addChangeEdge(Card *source, const Card *target)
{
    if (!source || !target)
        return false;
    const auto sourceToken = observeLive(source);
    const auto targetToken = observeLive(target);
    if (!sourceToken || !targetToken)
        return false;
    QMutexLocker lock(&m_mutex);
    if (source == target) {
        ++m_gauge.change_list_self_cycle;
        ++m_gauge.blocked_by_legacy_change_list;
        return false;
    }
    for (const ChangeEdge &edge : std::as_const(m_changeEdges)) {
        if (edge.source == source && edge.target == target) {
            if (edge.sourceToken->generation != sourceToken->generation ||
                edge.targetToken->generation != targetToken->generation)
                ++m_gauge.change_list_reuse_reconnect;
            else
                ++m_gauge.change_list_cycles;
            ++m_gauge.blocked_by_legacy_change_list;
            return false;
        }
    }
    QSet<const void *> visiting;
    std::function<bool(const void *)> reachesSource = [&](const void *node) {
        if (node == source)
            return true;
        if (visiting.contains(node))
            return false;
        visiting.insert(node);
        for (const ChangeEdge &edge : std::as_const(m_changeEdges)) {
            if (edge.source == node && reachesSource(edge.target))
                return true;
        }
        return false;
    };
    if (reachesSource(target)) {
        ++m_gauge.change_list_cycles;
        ++m_gauge.blocked_by_legacy_change_list;
        return false;
    }
    m_changeEdges.push_back(ChangeEdge{source, sourceToken, target, targetToken});
    ++m_gauge.sidecar_edges;
    return true;
}

void CardLifetimeManager::removeChangeEdges(const Card *card)
{
    if (!card)
        return;
    QMutexLocker lock(&m_mutex);
    for (auto it = m_changeEdges.begin(); it != m_changeEdges.end();) {
        if (it->source == card || it->target == card) {
            it = m_changeEdges.erase(it);
            if (m_gauge.sidecar_edges > 0)
                --m_gauge.sidecar_edges;
        } else {
            ++it;
        }
    }
}

bool CardLifetimeManager::bindTag(const void *container, const QByteArray &key, Card *card)
{
    if (!container || !card)
        return false;
    const auto token = observeCard(card);
    if (!token)
        return false;
    releaseTag(container, key);
    if (!retainNativeLease(token))
        return false;
    QMutexLocker lock(&m_mutex);
    m_tagBindings.push_back(TagBinding{container, key, token});
    return true;
}

void CardLifetimeManager::releaseTag(const void *container, const QByteArray &key)
{
    std::shared_ptr<const CardLifetimeToken> token;
    {
        QMutexLocker lock(&m_mutex);
        for (auto it = m_tagBindings.begin(); it != m_tagBindings.end(); ++it) {
            if (it->container == container && it->key == key) {
                token = it->token;
                m_tagBindings.erase(it);
                break;
            }
        }
    }
    if (token)
        releaseNativeLease(token);
}

void CardLifetimeManager::releaseTags(const void *container)
{
    QList<std::shared_ptr<const CardLifetimeToken>> tokens;
    {
        QMutexLocker lock(&m_mutex);
        for (auto it = m_tagBindings.begin(); it != m_tagBindings.end();) {
            if (it->container == container) {
                tokens.push_back(it->token);
                it = m_tagBindings.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (const auto &token : tokens)
        releaseNativeLease(token);
}

void CardLifetimeManager::retainEventPayload(const void *owner,
                                             std::initializer_list<const Card *> cards)
{
    if (!owner)
        return;
    releaseEventPayload(owner);
    QList<std::shared_ptr<const CardLifetimeToken>> tokens;
    for (const Card *card : cards) {
        if (!card)
            continue;
        const auto token = observeCard(const_cast<Card *>(card));
        if (token && retainNativeLease(token))
            tokens.push_back(token);
    }
    QMutexLocker lock(&m_mutex);
    if (!tokens.isEmpty())
        m_eventLeases.insert(owner, std::move(tokens));
}

void CardLifetimeManager::releaseEventPayload(const void *owner)
{
    QList<std::shared_ptr<const CardLifetimeToken>> tokens;
    {
        QMutexLocker lock(&m_mutex);
        auto found = m_eventLeases.find(owner);
        if (found == m_eventLeases.end())
            return;
        tokens = std::move(found.value());
        m_eventLeases.erase(found);
    }
    for (const auto &token : tokens)
        releaseNativeLease(token);
}

quint64 CardLifetimeManager::releaseEventPayloads(const void *domain)
{
    if (!domain)
        return 0;
    QList<std::shared_ptr<const CardLifetimeToken>> tokens;
    {
        QMutexLocker lock(&m_mutex);
        for (auto owner = m_eventLeases.begin(); owner != m_eventLeases.end();) {
            QList<std::shared_ptr<const CardLifetimeToken>> &heldTokens = owner.value();
            for (auto held = heldTokens.begin(); held != heldTokens.end();) {
                const Entry *entry = entryForLease(*held);
                if (entry && entry->domain == domain) {
                    tokens.append(*held);
                    held = heldTokens.erase(held);
                } else {
                    ++held;
                }
            }
            if (heldTokens.isEmpty())
                owner = m_eventLeases.erase(owner);
            else
                ++owner;
        }
    }
    for (const auto &token : std::as_const(tokens))
        releaseNativeLease(token);
    return static_cast<quint64>(tokens.size());
}

namespace {
// N8.3 frozen matrix: Card-bearing event structs that already hold their own
// native leases through their copy/assign/destructor hooks. A QVariant copy of
// one of these carries its own leases, so the payload is accepted as-is.
bool isSelfLeasingCardStruct(int userType)
{
    static const QSet<int> selfLeasing = {
        qMetaTypeId<CardUseStruct>(),
        qMetaTypeId<CardResponseStruct>(),
        qMetaTypeId<DamageStruct>(),
        qMetaTypeId<SlashEffectStruct>(),
        qMetaTypeId<RecoverStruct>(),
        // Card-id only payloads; their CardMoveReason registers its own extra data.
        qMetaTypeId<CardsMoveStruct>(),
        qMetaTypeId<CardsMoveOneTimeStruct>(),
        // Card-id only, and named "...Card..." purely by coincidence.
        qMetaTypeId<ShownCardChangedStruct>(),
    };
    return selfLeasing.contains(userType);
}
}

bool CardLifetimeManager::retainVariantPayload(const void *owner, const QVariant &value,
                                                QByteArray *error)
{
    if (!owner)
        return false;
    QList<const Card *> cards;
    std::function<bool(const QVariant &)> collect = [&](const QVariant &item) {
        if (!item.isValid())
            return true;
        if (item.userType() == qMetaTypeId<CardEffectStruct>()) {
            const CardEffectStruct effect = item.value<CardEffectStruct>();
            if (effect.card)
                cards.push_back(effect.card);
            if (effect.offset_card)
                cards.push_back(effect.offset_card);
            return true;
        }
        if (item.canConvert<CardTagOwner>()) {
            const Card *card = item.value<CardTagOwner>().card;
            if (card) cards.push_back(card);
            return true;
        }
        // SkillContext / CorrectSkillContext hold raw Card pointers behind type names that
        // the opaque-name heuristic below never matches, so they are extracted explicitly.
        if (item.userType() == qMetaTypeId<SkillContext>()) {
            const SkillContext context = item.value<SkillContext>();
            if (context.use_card)
                cards.push_back(context.use_card);
            if (context.updated_card)
                cards.push_back(context.updated_card);
            if (!collect(context.extra_data))
                return false;
            for (auto it = context.interceptor_data.cbegin();
                 it != context.interceptor_data.cend(); ++it)
                if (!collect(QVariant(it.value())))
                    return false;
            return true;
        }
        if (item.userType() == qMetaTypeId<CorrectSkillContext>()) {
            const CorrectSkillContext context = item.value<CorrectSkillContext>();
            if (context.card)
                cards.push_back(context.card);
            return true;
        }
        const char *typeName = QMetaType::typeName(item.userType());
        if (typeName && (qstrcmp(typeName, "Card*") == 0 || qstrcmp(typeName, "const Card*") == 0)) {
            cards.push_back(item.value<Card *>());
            return true;
        }
        if (item.userType() == QMetaType::QVariantList) {
            for (const QVariant &child : item.toList())
                if (!collect(child)) return false;
            return true;
        }
        if (item.userType() == QMetaType::QVariantMap) {
            const QVariantMap map = item.toMap();
            for (auto it = map.cbegin(); it != map.cend(); ++it)
                if (!collect(it.value())) return false;
            return true;
        }
        if (isSelfLeasingCardStruct(item.userType()))
            return true;
        if (typeName && (QByteArray(typeName).contains("Card") || QByteArray(typeName).contains("card"))) {
            if (error) *error = opaqueVariantError();
            QMutexLocker lock(&m_mutex);
            ++m_gauge.unknown_qvariant_card_payload;
            return false;
        }
        return true;
    };
    if (!collect(value))
        return false;
    releaseEventPayload(owner);
    QList<std::shared_ptr<const CardLifetimeToken>> tokens;
    for (const Card *card : cards) {
        const auto token = observeCard(const_cast<Card *>(card));
        if (!token || !retainNativeLease(token)) {
            for (const auto &held : tokens) releaseNativeLease(held);
            return false;
        }
        tokens.push_back(token);
    }
    QMutexLocker lock(&m_mutex);
    if (!tokens.isEmpty()) m_eventLeases.insert(owner, std::move(tokens));
    return true;
}

bool CardLifetimeManager::retainVariantTag(const void *container, const QByteArray &key,
                                           const QVariant &value, QByteArray *error)
{
    if (!retainVariantPayload(container, value, error))
        return false;
    releaseVariantTag(container, key);
    QList<std::shared_ptr<const CardLifetimeToken>> tokens;
    {
        QMutexLocker lock(&m_mutex);
        auto found = m_eventLeases.find(container);
        if (found != m_eventLeases.end()) {
            tokens = std::move(found.value());
            m_eventLeases.erase(found);
        }
        if (!tokens.isEmpty())
            m_variantTags[container].insert(key, std::move(tokens));
    }
    return true;
}

void CardLifetimeManager::releaseVariantTag(const void *container, const QByteArray &key)
{
    QList<std::shared_ptr<const CardLifetimeToken>> tokens;
    {
        QMutexLocker lock(&m_mutex);
        auto outer = m_variantTags.find(container);
        if (outer == m_variantTags.end()) return;
        auto inner = outer->find(key);
        if (inner == outer->end()) return;
        tokens = std::move(inner.value());
        outer->erase(inner);
        if (outer->isEmpty()) m_variantTags.erase(outer);
    }
    for (const auto &token : tokens) releaseNativeLease(token);
}

void CardLifetimeManager::releaseVariantTags(const void *container)
{
    QList<std::shared_ptr<const CardLifetimeToken>> tokens;
    {
        QMutexLocker lock(&m_mutex);
        auto outer = m_variantTags.find(container);
        if (outer == m_variantTags.end()) return;
        for (auto inner = outer->begin(); inner != outer->end(); ++inner)
            tokens.append(std::move(inner.value()));
        m_variantTags.erase(outer);
    }
    for (const auto &token : tokens) releaseNativeLease(token);
}

bool CardLifetimeManager::bindWrapper(const void *wrapper,
                                      const std::shared_ptr<const CardLifetimeToken> &token,
                                      bool originalOwner)
{
    if (!wrapper)
        return false;
    QMutexLocker lock(&m_mutex);
    Entry *entry = entryFor(token);
    if (!entry)
        return false;
    auto previous = m_wrappers.find(wrapper);
    if (previous != m_wrappers.end()) {
        const auto previousToken = previous->token;
        m_wrappers.erase(previous);
        if (Entry *previousEntry = entryForLease(previousToken)) {
            if (previousEntry->wrappers > 0)
                --previousEntry->wrappers;
            if (m_gauge.wrapper_leases > 0)
                --m_gauge.wrapper_leases;
            reapDeadLocked(previousToken);
        }
    }
    m_wrappers.insert(wrapper, WrapperBinding{token, originalOwner, currentDomain,
                                               currentRuntimeIdentity,
                                               currentRuntimeGeneration,
                                               currentRuntimeState});
    if (originalOwner)
        clearUnclaimedLocked(*entry);
    return true;
}

std::shared_ptr<const CardLifetimeToken> CardLifetimeManager::wrapperBinding(
    const void *wrapper, bool *originalOwner) const
{
    QMutexLocker lock(&m_mutex);
    const auto found = m_wrappers.constFind(wrapper);
    if (found == m_wrappers.cend())
        return {};
    if (found->runtimeIdentity
        && (found->runtimeIdentity != currentRuntimeIdentity
            || found->runtimeGeneration != currentRuntimeGeneration
            || found->runtimeState != currentRuntimeState))
        return {};
    if (originalOwner)
        *originalOwner = found->originalOwner;
    return found->token;
}

std::shared_ptr<const CardLifetimeToken> CardLifetimeManager::releaseWrapperBinding(
    const void *wrapper, bool *originalOwner)
{
    QMutexLocker lock(&m_mutex);
    auto found = m_wrappers.find(wrapper);
    if (found == m_wrappers.end())
        return {};
    if (found->runtimeIdentity
        && (found->runtimeIdentity != currentRuntimeIdentity
            || found->runtimeGeneration != currentRuntimeGeneration
            || found->runtimeState != currentRuntimeState))
        return {};
    const auto token = found->token;
    if (originalOwner)
        *originalOwner = found->originalOwner;
    m_wrappers.erase(found);
    Entry *entry = entryForLease(token);
    if (entry && entry->wrappers > 0) {
        --entry->wrappers;
        if (m_gauge.wrapper_leases > 0)
            --m_gauge.wrapper_leases;
    }
    reapDeadLocked(token);
    return token;
}

quint64 CardLifetimeManager::releaseWrapperBindings(const void *domain)
{
    if (!domain)
        return 0;
    QList<std::shared_ptr<const CardLifetimeToken>> owningTokens;
    QMutexLocker lock(&m_mutex);
    quint64 released = 0;
    for (auto it = m_wrappers.begin(); it != m_wrappers.end();) {
        if (it->domain != domain) {
            ++it;
            continue;
        }
        const auto token = it->token;
        const bool originalOwner = it->originalOwner;
        it = m_wrappers.erase(it);
        if (Entry *entry = entryForLease(token)) {
            if (entry->wrappers > 0)
                --entry->wrappers;
            if (m_gauge.wrapper_leases > 0)
                --m_gauge.wrapper_leases;
            reapDeadLocked(token);
        }
        if (originalOwner)
            owningTokens.push_back(token);
        ++released;
    }
    lock.unlock();
    for (const auto &token : owningTokens)
        requestLuaDelete(token);
    return released;
}

quint64 CardLifetimeManager::releaseWrapperBindings(const void *domain,
                                                   const void *identity,
                                                   quint64 generation,
                                                   lua_State *state)
{
    if (!domain || !identity || generation == 0 || !state)
        return 0;
    QList<std::shared_ptr<const CardLifetimeToken>> owningTokens;
    quint64 released = 0;
    {
        QMutexLocker lock(&m_mutex);
        for (auto it = m_wrappers.begin(); it != m_wrappers.end();) {
            if (it->domain != domain || it->runtimeIdentity != identity
                || it->runtimeGeneration != generation || it->runtimeState != state) {
                ++it;
                continue;
            }
            const auto token = it->token;
            const bool originalOwner = it->originalOwner;
            it = m_wrappers.erase(it);
            if (Entry *entry = entryForLease(token)) {
                if (entry->wrappers > 0)
                    --entry->wrappers;
                if (m_gauge.wrapper_leases > 0)
                    --m_gauge.wrapper_leases;
                reapDeadLocked(token);
            }
            if (originalOwner)
                owningTokens.push_back(token);
            ++released;
        }
    }
    for (const auto &token : owningTokens)
        requestLuaDelete(token);
    return released;
}

quint64 CardLifetimeManager::drain()
{
    struct Candidate {
        std::shared_ptr<const CardLifetimeToken> token;
        QObject *object = nullptr;
        QThread *affinityThread = nullptr;
    };
    QVector<Candidate> candidates;
    {
        QMutexLocker lock(&m_mutex);
        if (m_mode != CardLifetimeMode::ManagedReclaim
            || (m_ownerThread && QThread::currentThread() != m_ownerThread))
            return 0;
        reconcileDestroyedLocked();
        for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
            const bool domainBlocked = m_domainActiveScopes.value(it->domain, 0) > 0
                || m_domainLuaPins.value(it->domain, 0) > 0;
            if (!domainBlocked && it->pending && !it->nativeDelete && it->wrappers == 0
                && it->nativeLeases == 0 && it->adoptionReservations == 0
                && std::none_of(m_changeEdges.cbegin(), m_changeEdges.cend(), [&](const ChangeEdge &edge) {
                    return edge.sourceToken.get() == it->token.get() || edge.targetToken.get() == it->token.get();
                }))
                candidates.push_back({it->token, it->object, it->affinityThread});
        }
    }

    QVector<QObject *> objects;
    quint64 destroyed = 0;
    for (const Candidate &candidate : std::as_const(candidates)) {
        if (candidate.affinityThread && candidate.affinityThread != QThread::currentThread())
            continue;
        QMutexLocker lock(&m_mutex);
        auto it = m_entries.find(candidate.token->address);
        if (it == m_entries.end() || it->token.get() != candidate.token.get()
            || it->token->generation != candidate.token->generation
             || it->object != candidate.object
             || it->affinityThread != candidate.affinityThread
             || m_domainActiveScopes.value(it->domain, 0) > 0
             || m_domainLuaPins.value(it->domain, 0) > 0
             || !it->pending || it->nativeDelete || it->wrappers != 0
            || it->nativeLeases != 0 || it->adoptionReservations != 0
            || std::any_of(m_changeEdges.cbegin(), m_changeEdges.cend(), [&](const ChangeEdge &edge) {
                return edge.sourceToken.get() == it->token.get() || edge.targetToken.get() == it->token.get();
            }))
            continue;
        {
            it->pending = false;
            it->nativeDelete = true;
            it->token->live = false;
            it->token->state = CardLifetimeState::Retired;
            if (m_gauge.pending_delete > 0) --m_gauge.pending_delete;
            if (m_gauge.managed_live > 0) --m_gauge.managed_live;
            ++m_gauge.retired;
            ++destroyed;
            if (it->object)
                objects.push_back(it->object);
        }
    }
    for (QObject *object : objects)
        if (object)
            object->QObject::deleteLater();
    return destroyed;
}

bool CardLifetimeManager::finalizeWorkerDomain(const void *domain, quint64 *retired)
{
    if (retired)
        *retired = 0;
    if (!domain)
        return false;
    if (m_mode != CardLifetimeMode::ManagedReclaim)
        return true;

    struct Candidate {
        std::shared_ptr<const CardLifetimeToken> token;
        QPointer<QObject> object;
    };
    QVector<Candidate> candidates;
    {
        QMutexLocker lock(&m_mutex);
        reconcileDestroyedLocked();
        if (m_domainActiveScopes.value(domain, 0) != 0
            || m_domainLuaPins.value(domain, 0) != 0)
            return false;

        for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
            if (it->domain != domain || it->baselineDomain == domain
                || !it->physical || it->affinityThread != QThread::currentThread())
                continue;
            if (it->token->state == CardLifetimeState::Adopted)
                return false;
            if (it->token->originalOwner
                || it->token->state == CardLifetimeState::ObservedDefinition
                || !it->token->live)
                continue;
            const bool hasChangeEdge = std::any_of(
                m_changeEdges.cbegin(), m_changeEdges.cend(), [&](const ChangeEdge &edge) {
                    return edge.sourceToken.get() == it->token.get()
                        || edge.targetToken.get() == it->token.get();
                });
            if (it->nativeLeases != 0 || it->adoptionReservations != 0 || hasChangeEdge)
                return false;
            candidates.push_back({it->token, it->object});
        }

        for (const Candidate &candidate : std::as_const(candidates)) {
            auto it = m_entries.find(candidate.token->address);
            if (it == m_entries.end() || it->token.get() != candidate.token.get()
                || it->token->generation != candidate.token->generation
                || !it->token->live || it->domain != domain
                || it->affinityThread != QThread::currentThread())
                return false;
            if (it->pending) {
                it->pending = false;
                if (m_gauge.pending_delete > 0)
                    --m_gauge.pending_delete;
            }
            it->nativeDelete = true;
            it->token->live = false;
            it->token->state = CardLifetimeState::Retired;
            ++m_gauge.retired;
        }
    }

    for (const Candidate &candidate : std::as_const(candidates))
        if (candidate.object)
            delete candidate.object.data();

    {
        QMutexLocker lock(&m_mutex);
        reconcileDestroyedLocked();
        for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
            if (it->domain != domain || it->baselineDomain == domain
                || !it->physical || it->affinityThread != QThread::currentThread())
                continue;
            if (it->token->state == CardLifetimeState::Adopted)
                return false;
            if (!it->token->originalOwner && it->token->live)
                return false;
        }
    }
    if (retired)
        *retired = candidates.size();
    return true;
}

CardLifetimeGauge CardLifetimeManager::gauge() const
{
    QMutexLocker lock(&m_mutex);
    const_cast<CardLifetimeManager *>(this)->reconcileDestroyedLocked();
    return m_gauge;
}

CardLifetimeGauge CardLifetimeManager::gaugeForDomain(const void *domain) const
{
    QMutexLocker lock(&m_mutex);
    CardLifetimeGauge result;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        if (it->domain != domain || it->baselineDomain == domain)
            continue;
        if (it->token->live)
            ++result.managed_live;
        if (it->factoryUnclaimed)
            ++result.factory_unclaimed;
        if (it->unknownUnclaimed)
            ++result.unknown_unclaimed;
        if (it->pending)
            ++result.pending_delete;
        result.wrapper_leases += it->wrappers;
        result.native_leases += it->nativeLeases;
        result.adoption_reserved += it->adoptionReservations;
    }
    for (const ChangeEdge &edge : m_changeEdges) {
        const auto source = m_entries.constFind(edge.source);
        const auto target = m_entries.constFind(edge.target);
        const bool sourceInDomain = edge.sourceToken && source != m_entries.cend()
            && source->token.get() == edge.sourceToken.get()
            && source->token->generation == edge.sourceToken->generation
            && source->domain == domain;
        const bool targetInDomain = edge.targetToken && target != m_entries.cend()
            && target->token.get() == edge.targetToken.get()
            && target->token->generation == edge.targetToken->generation
            && target->domain == domain;
        if (sourceInDomain || targetInDomain)
            ++result.sidecar_edges;
    }
    result.lua_pins = m_domainLuaPins.value(domain, 0);
    return result;
}

void CardLifetimeManager::registerRuntimeDomain(const void *domain, const void *identity,
                                                quint64 generation, lua_State *state)
{
    if (!domain || !identity || generation == 0)
        return;
    QMutexLocker lock(&m_mutex);
    for (const RuntimeRegistration &registration : std::as_const(m_runtimeRegistrations)) {
        if (registration.domain == domain && registration.identity == identity
            && registration.generation == generation && registration.state == state)
            return;
    }
    RuntimeRegistration registration;
    registration.domain = domain;
    registration.identity = identity;
    registration.generation = generation;
    registration.state = state;
    m_runtimeRegistrations.push_back(std::move(registration));
}

void CardLifetimeManager::unregisterRuntimeDomain(const void *domain, const void *identity,
                                                  quint64 generation, lua_State *state)
{
    QMutexLocker lock(&m_mutex);
    for (auto it = m_runtimeRegistrations.begin(); it != m_runtimeRegistrations.end(); ++it) {
        if (it->domain == domain && it->identity == identity
            && it->generation == generation && it->state == state) {
            m_runtimeRegistrations.erase(it);
            auto runtimePins = m_runtimeLuaPins.find(identity);
            if (runtimePins != m_runtimeLuaPins.end()) {
                runtimePins->remove(generation);
                if (runtimePins->isEmpty())
                    m_runtimeLuaPins.erase(runtimePins);
            }
            return;
        }
    }
}

CardLifetimeGauge CardLifetimeManager::gaugeForRuntime(const void *domain,
                                                       const void *identity,
                                                       quint64 generation,
                                                       lua_State *state) const
{
    QMutexLocker lock(&m_mutex);
    CardLifetimeGauge result;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        if (it->domain != domain || it->runtimeIdentity != identity
            || it->runtimeGeneration != generation || it->runtimeState != state)
            continue;
        if (it->baselineDomain == domain)
            continue;
        if (it->token->live)
            ++result.managed_live;
        if (it->factoryUnclaimed)
            ++result.factory_unclaimed;
        if (it->unknownUnclaimed)
            ++result.unknown_unclaimed;
        if (it->pending)
            ++result.pending_delete;
        result.wrapper_leases += it->wrappers;
        result.native_leases += it->nativeLeases;
        result.adoption_reserved += it->adoptionReservations;
    }
    for (const ChangeEdge &edge : std::as_const(m_changeEdges)) {
        const auto source = m_entries.constFind(edge.source);
        const auto target = m_entries.constFind(edge.target);
        const bool sourceInRuntime = edge.sourceToken && source != m_entries.cend()
            && source->token.get() == edge.sourceToken.get()
            && source->token->generation == edge.sourceToken->generation
            && source->domain == domain && source->runtimeIdentity == identity
            && source->runtimeGeneration == generation && source->runtimeState == state;
        const bool targetInRuntime = edge.targetToken && target != m_entries.cend()
            && target->token.get() == edge.targetToken.get()
            && target->token->generation == edge.targetToken->generation
            && target->domain == domain && target->runtimeIdentity == identity
            && target->runtimeGeneration == generation && target->runtimeState == state;
        if (sourceInRuntime || targetInRuntime)
            ++result.sidecar_edges;
    }
    result.lua_pins = m_runtimeLuaPins.value(identity).value(generation, 0);
    return result;
}

void CardLifetimeManager::setDomainBaseline(const void *domain,
                                             const QSet<const void *> &addresses)
{
    QMutexLocker lock(&m_mutex);
    QHash<const void *, quint64> generations;
    for (const void *address : addresses) {
        const auto found = m_entries.constFind(address);
        if (found != m_entries.cend()) {
            generations.insert(address, found->token->generation);
            auto entry = m_entries.find(address);
            entry->baselineDomain = domain;
        }
    }
    if (generations.isEmpty())
        m_domainBaselines.remove(domain);
    else
        m_domainBaselines.insert(domain, std::move(generations));
}

void CardLifetimeManager::unregisterDomainBaseline(const void *domain)
{
    QMutexLocker lock(&m_mutex);
    m_domainBaselines.remove(domain);
}

void CardLifetimeManager::dumpDomain(const void *domain) const
{
    QList<const void *> liveAddresses;
    QMutexLocker lock(&m_mutex);
    quint64 live = 0;
    quint64 original = 0;
    quint64 external = 0;
    quint64 adopted = 0;
    quint64 pending = 0;
    quint64 withObject = 0;
    quint64 entries = 0;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        if (it->domain != domain || it->baselineDomain == domain)
            continue;
        if (it->token->live)
            ++live;
        if (it->token->originalOwner)
            ++original;
        if (it->token->state == CardLifetimeState::ObservedExternal)
            ++external;
        if (it->token->state == CardLifetimeState::Adopted)
            ++adopted;
        if (it->pending)
            ++pending;
        if (it->object)
            ++withObject;
    }
    quint64 printed = 0;
    for (auto it = m_entries.cbegin(); it != m_entries.cend() && printed < 128; ++it) {
        if (it->domain != domain || !it->token->live
            || it->baselineDomain == domain)
            continue;
        liveAddresses.push_back(it.key());
        ++printed;
    }
    entries = static_cast<quint64>(m_entries.size());
    lock.unlock();
    for (const void *address : liveAddresses)
        std::fprintf(stderr, "CARD_LIFETIME_DOMAIN_ENTRY address=%p\n", address);
    std::fprintf(stderr, "CARD_LIFETIME_DOMAIN_DUMP live=%llu original=%llu external=%llu adopted=%llu pending=%llu object=%llu entries=%llu\n",
                 static_cast<unsigned long long>(live),
                 static_cast<unsigned long long>(original),
                 static_cast<unsigned long long>(external),
                 static_cast<unsigned long long>(adopted),
                 static_cast<unsigned long long>(pending),
                 static_cast<unsigned long long>(withObject),
                 static_cast<unsigned long long>(entries));
}

quint64 CardLifetimeManager::entryCount() const
{
    QMutexLocker lock(&m_mutex);
    return static_cast<quint64>(m_entries.size());
}

QSet<const void *> CardLifetimeManager::entryAddresses() const
{
    QMutexLocker lock(&m_mutex);
    QSet<const void *> addresses;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it)
        addresses.insert(it.key());
    return addresses;
}

QSet<const void *> CardLifetimeManager::entryAddressesForDomain(const void *domain) const
{
    QMutexLocker lock(&m_mutex);
    QSet<const void *> addresses;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it)
        if (it->domain == domain && it->baselineDomain != domain)
            addresses.insert(it.key());
    return addresses;
}

quint64 CardLifetimeManager::entryCountForDomain(const void *domain) const
{
    QMutexLocker lock(&m_mutex);
    quint64 count = 0;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it)
        if (it->domain == domain && it->baselineDomain != domain)
            ++count;
    return count;
}

quint64 CardLifetimeManager::activeScopeDepth() const
{
    QMutexLocker lock(&m_mutex);
    return m_activeScopes;
}

quint64 CardLifetimeManager::activeScopeDepthForDomain(const void *domain) const
{
    QMutexLocker lock(&m_mutex);
    return m_domainActiveScopes.value(domain, 0);
}

void CardLifetimeManager::enterScope()
{
    QMutexLocker lock(&m_mutex);
    ++m_activeScopes;
    ++m_domainActiveScopes[currentDomain];
}

void CardLifetimeManager::leaveScope()
{
    QMutexLocker lock(&m_mutex);
    if (m_activeScopes > 0)
        --m_activeScopes;
    auto found = m_domainActiveScopes.find(currentDomain);
    if (found != m_domainActiveScopes.end()) {
        if (*found > 0)
            --*found;
        if (*found == 0)
            m_domainActiveScopes.erase(found);
    }
}

void CardLifetimeManager::enterLuaPin()
{
    QMutexLocker lock(&m_mutex);
    ++m_luaPins;
    ++m_gauge.lua_pins;
    ++m_domainLuaPins[currentDomain];
    if (currentRuntimeIdentity && currentRuntimeGeneration != 0)
        ++m_runtimeLuaPins[currentRuntimeIdentity][currentRuntimeGeneration];
}

void CardLifetimeManager::leaveLuaPin()
{
    QMutexLocker lock(&m_mutex);
    if (m_luaPins > 0)
        --m_luaPins;
    if (m_gauge.lua_pins > 0)
        --m_gauge.lua_pins;
    auto found = m_domainLuaPins.find(currentDomain);
    if (found != m_domainLuaPins.end()) {
        if (*found > 0)
            --*found;
        if (*found == 0)
            m_domainLuaPins.erase(found);
    }
    if (currentRuntimeIdentity && currentRuntimeGeneration != 0) {
        auto runtimePins = m_runtimeLuaPins.find(currentRuntimeIdentity);
        if (runtimePins != m_runtimeLuaPins.end()) {
            auto pinCount = runtimePins->find(currentRuntimeGeneration);
            if (pinCount != runtimePins->end()) {
                if (*pinCount > 0)
                    --*pinCount;
                if (*pinCount == 0)
                    runtimePins->erase(pinCount);
            }
            if (runtimePins->isEmpty())
                m_runtimeLuaPins.erase(runtimePins);
        }
    }
}

quint64 CardLifetimeManager::luaPinDepth() const
{
    QMutexLocker lock(&m_mutex);
    return m_luaPins;
}

bool CardLifetimeManager::resetForTest()
{
    QMutexLocker lock(&m_mutex);
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        if (it->token->live || it->pending || it->wrappers > 0 || it->nativeLeases > 0
            || it->adoptionReservations > 0 || it->object != nullptr)
            return false;
    }
    if (!m_wrappers.isEmpty() || !m_changeEdges.isEmpty() || !m_tagBindings.isEmpty()
        || !m_eventLeases.isEmpty() || !m_variantTags.isEmpty()
        || !m_domainActiveScopes.isEmpty() || !m_domainLuaPins.isEmpty()
        || !m_runtimeLuaPins.isEmpty()
        || !m_runtimeRegistrations.isEmpty() || !m_domainBaselines.isEmpty()
        || m_activeScopes != 0 || m_luaPins != 0 || m_gauge.managed_live != 0
        || m_gauge.pending_delete != 0 || m_gauge.wrapper_leases != 0
        || m_gauge.native_leases != 0 || m_gauge.lua_pins != 0
        || m_gauge.adoption_reserved != 0 || m_gauge.sidecar_edges != 0
        || m_gauge.factory_unclaimed != 0 || m_gauge.unknown_unclaimed != 0
        || !m_deadEntries.isEmpty())
        return false;
    m_entries.clear();
    m_deadEntries.clear();
    m_wrappers.clear();
    m_domainActiveScopes.clear();
    m_domainLuaPins.clear();
    m_runtimeLuaPins.clear();
    m_domainBaselines.clear();
    m_gauge = {};
    return true;
}

CardLifetimeLease::CardLifetimeLease(CardLifetimeManager &manager,
                                     std::shared_ptr<const CardLifetimeToken> token)
    : m_manager(&manager), m_token(std::move(token))
{
    if (!m_manager->retainNativeLease(m_token)) {
        m_manager = nullptr;
        m_token.reset();
    }
}

CardLifetimeLease::~CardLifetimeLease()
{
    if (m_manager && m_token)
        m_manager->releaseNativeLease(m_token);
}

CardLifetimeLease::CardLifetimeLease(CardLifetimeLease &&other) noexcept
    : m_manager(other.m_manager), m_token(std::move(other.m_token))
{
    other.m_manager = nullptr;
}

CardLifetimeLease &CardLifetimeLease::operator=(CardLifetimeLease &&other) noexcept
{
    if (this == &other)
        return *this;
    if (m_manager && m_token)
        m_manager->releaseNativeLease(m_token);
    m_manager = other.m_manager;
    m_token = std::move(other.m_token);
    other.m_manager = nullptr;
    return *this;
}

void CardLifetimeManager::updatePeaksLocked()
{
    m_gauge.peak_managed_cards = qMax(m_gauge.peak_managed_cards, m_gauge.managed_live);
    m_gauge.peak_pending_delete = qMax(m_gauge.peak_pending_delete, m_gauge.pending_delete);
    m_gauge.peak_wrapper_count = qMax(m_gauge.peak_wrapper_count, m_gauge.wrapper_leases);
}

const char *CardLifetimeManager::staleCardError() { return "Lua error: attempt to use deleted Card"; }
const char *CardLifetimeManager::unknownOwnershipError() { return "Lua error: attempt to delete Card with unknown ownership"; }
const char *CardLifetimeManager::opaqueVariantError() { return "Card lifetime error: rejected opaque QVariant Card payload"; }

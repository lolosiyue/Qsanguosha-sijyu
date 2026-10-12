#include "wrapped-card.h"
#include "ai-strategy/inovation-ai.h"
#include "card-lifetime-manager.h"

#include <QMetaObject>
#include <QSemaphore>
#include <QTimer>

namespace {
template<typename Function>
bool invokeBlocking(QObject *context, Function function)
{
    if (!context)
        return false;
    if (QThread::currentThread() == context->thread()) {
        function();
        return true;
    }
#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
    QSemaphore completed;
    QTimer::singleShot(0, context, [function, &completed]() mutable {
        function();
        completed.release();
    });
    completed.acquire();
    return true;
#else
    return QMetaObject::invokeMethod(context, function, Qt::BlockingQueuedConnection);
#endif
}

bool moveCardToOwner(Card *card, QThread *ownerThread)
{
    if (!card || !ownerThread || card->parent())
        return false;
    if (card->thread() == ownerThread)
        return true;
    bool moved = false;
    if (QThread::currentThread() == card->thread()) {
        card->moveToThread(ownerThread);
        return card->thread() == ownerThread;
    }
    const bool invoked = invokeBlocking(card, [&] {
        card->moveToThread(ownerThread);
        moved = card->thread() == ownerThread;
    });
    return invoked && moved;
}

bool ownerDispatchAvailable(Card *card, QThread *ownerThread)
{
    if (!card)
        return true;
    if (!ownerThread || card->thread() != ownerThread || card->parent())
        return false;
    if (QThread::currentThread() == ownerThread)
        return true;
    return invokeBlocking(card, [] {});
}

bool destroyOwnedCard(Card *card)
{
    if (!card)
        return true;
    if (QThread::currentThread() == card->thread()) {
        delete card;
        return true;
    }
    return invokeBlocking(card, [card] { delete card; });
}
}

WrappedCard::WrappedCard(Card *card)
    : Card(card ? card->getSuit() : Card::SuitToBeDecided,
           card ? card->getNumber() : -1), m_card(nullptr), m_isModified(false),
      m_adoptionOwnerThread(QThread::currentThread())
{
    if (card) {
        retireAIRecursiveFieldOrigins(16u);
        m_id = card->getId();
        copyEverythingFrom(card);
    }
}

// Private, once per object, and called only by the public-definition factory
// loop. A generic constructor/adoption never obtains this structural receipt.
WrappedCard::WrappedCard(PhysicalCloneBirth, Card *clone, int definitionId, QThread *ownerThread,
                         bool originalDefinitionSource, bool originalDefinitionIdSource)
    : WrappedCard(nullptr)
{
    // The actual public factory source, not a later selected-object scan.
    const bool originalClone = originalDefinitionSource && clone
        && clone->aiIdWriteEpoch() == 1
        && clone->aiMaterialWriteEpoch() == 1
        && (clone->aiOriginalRecursiveFieldOrigins() & 14u) == 14u;
    const bool originalCloneId = originalDefinitionIdSource && clone
        && clone->aiIdWriteEpoch() == 1;
    setAdoptionOwnerThread(ownerThread);
    copyEverythingFrom(clone);
    // Exact writer postcondition, not a scan/regrant of an existing instance.
    if (clone && m_card == clone) {
        m_aiBornInnerType = &typeid(*clone);
        m_aiBornDefinitionId = definitionId;
        m_aiBornInnerLifetime = globalCardLifetimeManager().liveToken(clone);
        m_aiOriginalCloneIdentity = originalClone && m_aiBornInnerLifetime
            && aiIdWriteEpoch() == 1 && aiMaterialWriteEpoch() == 1
            && clone->aiIdWriteEpoch() == 2 && clone->aiMaterialWriteEpoch() == 2
            && aiScalarWriteEpoch() == 2
            && (aiOriginalRecursiveFieldOrigins() & 2u) == 2u
            && (clone->aiOriginalRecursiveFieldOrigins() & 14u) == 14u;
        m_aiOriginalCloneId = originalCloneId && m_aiBornInnerLifetime
            && aiIdWriteEpoch() == 1 && clone->aiIdWriteEpoch() == 2;
        m_aiPhysicalCloneBirth.store(true, std::memory_order_release);
    }
}

bool WrappedCard::aiPhysicalCloneBirthOrigin(int *definitionId,
                                                   const std::type_info **innerType) const noexcept
{
    if (!m_aiPhysicalCloneBirth.load(std::memory_order_acquire)) return false;
    if (definitionId) *definitionId = m_aiBornDefinitionId;
    if (innerType) *innerType = m_aiBornInnerType;
    return true;
}

bool WrappedCard::aiPhysicalCloneIdentityOrigin(int *definitionId) const noexcept
{
    if (!m_aiPhysicalCloneBirth.load(std::memory_order_acquire)
        || !m_aiOriginalCloneIdentity || !m_aiBornInnerLifetime
        || !globalCardLifetimeManager().isLive(m_aiBornInnerLifetime)) return false;
    const auto *inner = static_cast<const Card *>(m_aiBornInnerLifetime->address);
    if (aiIdWriteEpoch() != 1 || aiMaterialWriteEpoch() != 1
        || inner->aiIdWriteEpoch() != 2 || inner->aiMaterialWriteEpoch() != 2
        || aiScalarWriteEpoch() != 2 || (aiOriginalRecursiveFieldOrigins() & 2u) != 2u
        || (inner->aiOriginalRecursiveFieldOrigins() & 14u) != 14u)
        return false;
    if (definitionId) *definitionId = m_aiBornDefinitionId;
    return true;
}

bool WrappedCard::aiPhysicalCloneIdOrigin(int *definitionId) const noexcept
{
    if (!m_aiPhysicalCloneBirth.load(std::memory_order_acquire)
        || !m_aiOriginalCloneId || !m_aiBornInnerLifetime
        || !globalCardLifetimeManager().isLive(m_aiBornInnerLifetime)) return false;
    // As with the stronger lineage above, callers require native confinement.
    // The lifetime token is an identity check, not a pin or a field mutex.
    const auto *inner = static_cast<const Card *>(m_aiBornInnerLifetime->address);
    if (aiIdWriteEpoch() != 1 || inner->aiIdWriteEpoch() != 2)
        return false;
    if (definitionId) *definitionId = m_aiBornDefinitionId;
    return true;
}

const Card *WrappedCard::aiPhysicalCloneOwnedFaceOrigin() const noexcept
{
    // The original RoomState constructor records this token after successful
    // first adoption. Every later adoption attempt (even equal/null/failed)
    // permanently clears the birth bit BEFORE touching m_card. Mutable face
    // fields need not be pristine; this receipt proves custody, not values.
    // Retain the scope's independently required canonical physical-ID history
    // continuously as well; equal ID writes cannot keep a stale policy grant.
    if (!aiPhysicalCloneIdOrigin(nullptr)) return nullptr;
    return static_cast<const Card *>(m_aiBornInnerLifetime->address);
}

WrappedCard::~WrappedCard()
{
    destroyOwnedCard(m_card);
}

void WrappedCard::takeOver(Card *card)
{
	adoptCard(card, false);
}

void WrappedCard::copyEverythingFrom(Card *card)
{
	adoptCard(card, true);
}

void WrappedCard::setAdoptionOwnerThread(QThread *thread)
{
    m_adoptionOwnerThread = thread ? thread : QThread::currentThread();
}

void WrappedCard::adoptCard(Card *card, bool requireId)
{
    // Even null, equal, failed ownership or failed id attempts retire forever.
    m_aiPhysicalCloneBirth.store(false, std::memory_order_release);
    if (!card || card == this || m_card == card || (requireId && card->getId() < 0))
        return;

    CardLifetimeManager &lifetimeManager = globalCardLifetimeManager();
    const auto token = lifetimeManager.observeCard(card);
    CardLifetimeLease lease(lifetimeManager, token);
    if (!lease.isValid() || !lifetimeManager.reserveAdoption(token))
        return;

    if (!ownerDispatchAvailable(m_card, m_adoptionOwnerThread)
        || !moveCardToOwner(card, m_adoptionOwnerThread)) {
        lifetimeManager.cancelAdoption(token, true);
        return;
    }

    // observeCard() ran before moveCardToOwner(), so refresh the manager's
    // recorded QObject affinity before the worker-exit lifetime sweep.
    // Otherwise a successfully adopted card still looks worker-owned and makes
    // RoomRuntime::finalizeWorker() reject a clean game shutdown.
    lifetimeManager.observeCard(card);

    const bool adopted = lifetimeManager.markAdopted(token);
    if (!adopted) {
        Q_ASSERT(adopted);
        lifetimeManager.cancelAdoption(token, true);
        destroyOwnedCard(card);
        return;
    }

    Card *oldCard = m_card;
    m_card = nullptr;
    if (oldCard) {
        m_isModified = true;
        // Legacy takeOver()/copyEverythingFrom() hand the retired inner card's
        // tags (and, for takeOver, its flags) to the replacement before retiring it.
        for (auto it = oldCard->tag.cbegin(); it != oldCard->tag.cend(); ++it)
            card->setTag(it.key(), it.value());
        if (!requireId)
            card->setFlags(oldCard->getFlags());
        // The replaced card may still be the running receiver (for example a
        // nullified view-as DelayedTrick whose onNullified() throws itself into the
        // discard pile, which resets this wrapper), and event payloads such as
        // CardMoveReason::m_extraData lease its raw pointer. Deleting it here freed
        // it under those holders; hand it back to lease-gated reclamation instead.
        if (!lifetimeManager.retireAdopted(oldCard, moveCardToOwner)) {
            const bool destroyed = destroyOwnedCard(oldCard);
            Q_ASSERT(destroyed);
        }
    }
    m_card = card;
    lifetimeManager.cancelAdoption(token);
    // copyEverythingFrom() adopts the inner card's id onto the wrapper; takeOver()
    // keeps the wrapper's id and stamps it onto the new inner card.
    if (requireId)
        Card::setId(card->getId());
    const int wrapperId = getId();
    if (wrapperId >= 0)
        m_card->Card::setId(wrapperId);
    Card::setSuit(card->getSuit());
    Card::setNumber(card->getNumber());
    m_skillName = card->getSkillName(false);
    m_skillInstanceId = card->getSkillInstanceId();
    // Replacement/reset takes the new inner card's provenance, never the retired card's tags.
    m_appliedPhysicalEffectSource = card->m_appliedPhysicalEffectSource;
    setSourceSkill(card->getSourceSkillName(), card->getSourceSkillInstanceId());
    setActivationSkill(card->getActivationSkillName(), card->getActivationSkillInstanceId());
    setObjectName(card->objectName());
    if (requireId) {
        invalidateAIXiedouFlags(this);
        flags = card->getFlags();
    }
    // Both takeOver and copyEverythingFrom/reset converge here. Failed and
    // same-inner-card adoptions returned above; initial construction has no
    // previous live identity. A new inner instance can have different behavior
    // even when its printed name is unchanged.
    if (oldCard)
        emit identityChanged();
}

void WrappedCard::setFlags(const QString &flag) const
{
    //Q_ASSERT(m_card != nullptr);
    //m_isModified = true;
    Card::setFlags(flag);
    m_card->setFlags(flag);
}

void WrappedCard::setTag(const QString &key, const QVariant &data) const
{
    //Q_ASSERT(m_card != nullptr);
    //m_isModified = true;
    Card::setTag(key, data);
    m_card->setTag(key, data);
}

void WrappedCard::removeTag(const QString &key) const
{
    //Q_ASSERT(m_card != nullptr);
    //m_isModified = true;
    Card::removeTag(key);
    m_card->removeTag(key);
}

void WrappedCard::setMark(const QString &mark, int value) const
{
    //Q_ASSERT(m_card != nullptr);
    //m_isModified = true;
    Card::setMark(mark, value);
    m_card->setMark(mark, value);
}

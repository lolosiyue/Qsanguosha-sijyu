#include "wrapped-card.h"
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
        m_id = card->getId();
        copyEverythingFrom(card);
    }
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
        // tags (and, for takeOver, its flags) to the replacement before deleting.
        for (auto it = oldCard->tag.cbegin(); it != oldCard->tag.cend(); ++it)
            card->setTag(it.key(), it.value());
        if (!requireId)
            card->setFlags(oldCard->getFlags());
        const bool destroyed = destroyOwnedCard(oldCard);
        Q_ASSERT(destroyed);
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
    setSourceSkill(card->getSourceSkillName(), card->getSourceSkillInstanceId());
    setActivationSkill(card->getActivationSkillName(), card->getActivationSkillInstanceId());
    setObjectName(card->objectName());
    if (requireId)
        flags = card->getFlags();
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

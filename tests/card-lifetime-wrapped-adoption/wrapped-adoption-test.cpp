#include "card-lifetime-manager.h"
#include "structs.h"
#include "wrapped-card.h"

#include <QCoreApplication>
#include <QPointer>
#include <QThread>

#include <atomic>
#include <cstdio>
#include <memory>

namespace {
int failures = 0;
int checks = 0;

void check(bool condition, const char *name)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAIL %s\n", name);
    }
}

DummyCard *makeCard(int id, const char *tagName, const char *tagValue)
{
    auto *card = new DummyCard;
    card->setId(id);
    card->setTag(QString::fromLatin1(tagName), QString::fromLatin1(tagValue));
    return card;
}

void replacementPreservesIncomingAndRetiresOld()
{
    CardLifetimeManager &manager = globalCardLifetimeManager();
    check(manager.resetForTest(), "replacement.reset");
    quint64 destroyedBefore = manager.gauge().actually_destroyed;
    {
        auto *oldCard = makeCard(7001, "old", "old-value");
        WrappedCard outer(oldCard);
        const auto oldToken = manager.observeCard(oldCard);
        auto *incoming = makeCard(7001, "incoming", "incoming-value");
        const auto incomingToken = manager.observeCard(incoming);
        outer.copyEverythingFrom(incoming);
        check(outer.getRealCard() == incoming, "replacement.incoming-installed");
        check(incoming->getTag("incoming").toString() == "incoming-value",
              "replacement.incoming-tag-preserved");
        check(manager.state(incomingToken) == CardLifetimeState::Adopted,
              "replacement.exact-generation-adopted");
        // Replacement hands the old inner card to lease-gated reclamation; it is
        // not destroyed inside copyEverythingFrom().
        check(manager.state(oldToken) == CardLifetimeState::PendingDelete,
              "replacement.old-pending-not-destroyed");
        check(manager.gauge().actually_destroyed == destroyedBefore,
              "replacement.old-not-destroyed-inline");
        manager.drain();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        check(manager.gauge().actually_destroyed == destroyedBefore + 1,
              "replacement.old-retired-once");
        check(manager.state(oldToken) == CardLifetimeState::Dead,
              "replacement.old-generation-dead");
    }
    const auto gauge = manager.gauge();
    check(gauge.adoption_reserved == 0, "replacement.reservations-zero");
    check(gauge.native_leases == 0, "replacement.leases-zero");
    check(gauge.managed_live == 0, "replacement.managed-zero");
}

// Regression: a CardMoveReason whose m_extraData points at the inner card holds a
// native lease on it. Replacing that inner card (Room::resetCard when a view-as
// delayed trick is nullified into the discard pile) used to delete it on the spot,
// and the next CardMoveReason copy dereferenced the freed Card in
// retainVariantPayload().
void replacedInnerCardSurvivesWhileLeased()
{
    CardLifetimeManager &manager = globalCardLifetimeManager();
    check(manager.resetForTest(), "leased-replace.reset");
    {
        auto *first = makeCard(7004, "first", "first-value");
        const QPointer<Card> firstGuard(first);
        WrappedCard outer(first);
        const auto firstToken = manager.liveToken(first);
        {
            CardMoveReason source;
            source.m_extraData = QVariant::fromValue(static_cast<const Card *>(first));
            const CardMoveReason leased(source);
            check(manager.gauge().native_leases > 0, "leased-replace.payload-leased");

            outer.copyEverythingFrom(makeCard(7004, "replacement", "replacement-value"));
            check(!firstGuard.isNull(), "leased-replace.old-alive-after-replacement");
            manager.drain();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            check(!firstGuard.isNull(), "leased-replace.old-alive-while-leased");
            if (!firstGuard.isNull()) {
                const CardMoveReason copied(leased);
                check(copied.m_extraData.value<const Card *>() == first,
                      "leased-replace.payload-copy-still-valid");
            }
        }
        manager.drain();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        check(firstGuard.isNull(), "leased-replace.old-reclaimed-after-release");
        check(manager.state(firstToken) == CardLifetimeState::Dead,
              "leased-replace.old-generation-dead");
    }
    const auto gauge = manager.gauge();
    check(gauge.pending_delete == 0, "leased-replace.pending-zero");
    check(gauge.native_leases == 0, "leased-replace.leases-zero");
    check(gauge.managed_live == 0, "leased-replace.managed-zero");
}

// Production shape: the Room worker adopts into a wrapper owned by the canonical
// thread. The replaced inner card must come back to the registered turn-reclaim
// worker so drainTurnDomain() frees it there once the payload lease is gone.
void workerRetiresReplacedCardAtTurnEnd()
{
    CardLifetimeManager &manager = globalCardLifetimeManager();
    check(manager.resetForTest(), "turn-replace.reset");
    static int turnDomain = 0;
    std::atomic<bool> ok {true};
    std::atomic<bool> done {false};
    QThread *destroyedOn = nullptr;
    QPointer<Card> firstGuard;
    {
        WrappedCard outer(nullptr);
        std::unique_ptr<QThread> worker(QThread::create([&] {
            const void *previous = CardLifetimeManager::setCurrentDomain(&turnDomain);
            ok = manager.beginTurnReclamation(&turnDomain);
            auto *first = makeCard(7005, "first", "first-value");
            firstGuard = first;
            QObject::connect(first, &QObject::destroyed, [&destroyedOn] {
                destroyedOn = QThread::currentThread();
            });
            outer.copyEverythingFrom(first);
            ok = ok && outer.getRealCard() == first;

            CardMoveReason source;
            source.m_extraData = QVariant::fromValue(static_cast<const Card *>(first));
            auto leased = std::make_unique<CardMoveReason>(source);

            outer.copyEverythingFrom(makeCard(7005, "replacement", "replacement-value"));
            ok = ok && !firstGuard.isNull();
            ok = ok && !firstGuard.isNull() && first->thread() == QThread::currentThread();
            ok = ok && manager.drainTurnDomain(&turnDomain) == 0 && !firstGuard.isNull();
            if (!firstGuard.isNull()) {
                const CardMoveReason copied(*leased);
                ok = ok && copied.m_extraData.value<const Card *>() == first;
            }

            leased.reset();
            ok = ok && manager.drainTurnDomain(&turnDomain) == 1;
            ok = ok && firstGuard.isNull() && destroyedOn == QThread::currentThread();
            manager.endTurnReclamation(&turnDomain);
            CardLifetimeManager::setCurrentDomain(previous);
            done = true;
        }));
        worker->start();
        // The worker blocks on the canonical thread for adoption transfers.
        while (!done.load()) {
            QCoreApplication::processEvents();
            QThread::msleep(1);
        }
        worker->wait();
    }
    check(ok.load(), "turn-replace.worker-contract");
    const auto gauge = manager.gauge();
    check(gauge.pending_delete == 0, "turn-replace.pending-zero");
    check(gauge.native_leases == 0, "turn-replace.leases-zero");
    check(gauge.managed_live == 0, "turn-replace.managed-zero");
}

void rollbackPreservesOldOnAffinityFailure()
{
    CardLifetimeManager &manager = globalCardLifetimeManager();
    check(manager.resetForTest(), "rollback.reset");
    {
        auto *oldCard = makeCard(7002, "old", "old-value");
        WrappedCard outer(oldCard);
        QObject parent;
        auto *incoming = makeCard(7002, "incoming", "incoming-value");
        incoming->setParent(&parent);
        const auto incomingToken = manager.observeCard(incoming);
        outer.copyEverythingFrom(incoming);
        check(outer.getRealCard() == oldCard, "rollback.old-card-preserved");
        check(manager.state(incomingToken) == CardLifetimeState::ObservedExternal,
              "rollback.incoming-not-adopted");
        check(manager.gauge().adoption_reserved == 0, "rollback.reservation-cancelled");
        check(manager.gauge().affinity_transfer_failed > 0,
              "rollback.transfer-failure-measured");
    }
    const auto gauge = manager.gauge();
    check(gauge.adoption_reserved == 0, "rollback.final-reservations-zero");
    check(gauge.native_leases == 0, "rollback.final-leases-zero");
}

void canonicalOwnerTransfer()
{
    CardLifetimeManager &manager = globalCardLifetimeManager();
    check(manager.resetForTest(), "owner.reset");
    QThread worker;
    worker.start();
    {
        auto *oldCard = makeCard(7003, "old", "old-value");
        WrappedCard outer(oldCard);
        auto *incoming = makeCard(7003, "incoming", "incoming-value");
        incoming->moveToThread(&worker);
        outer.copyEverythingFrom(incoming);
        check(outer.getRealCard() == incoming, "owner.incoming-installed");
        check(incoming->thread() == QThread::currentThread(), "owner.canonical-thread");
        const auto incomingToken = manager.liveToken(incoming);
        check(incomingToken
                  && manager.affinityThread(incomingToken) == QThread::currentThread(),
              "owner.manager-affinity-refreshed");
    }
    worker.quit();
    worker.wait();
    manager.drain();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const auto gauge = manager.gauge();
    check(gauge.pending_delete == 0, "owner.replaced-card-reclaimed");
    check(gauge.adoption_reserved == 0, "owner.reservations-zero");
    check(gauge.native_leases == 0, "owner.leases-zero");
    check(gauge.managed_live == 0, "owner.managed-zero");
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    replacementPreservesIncomingAndRetiresOld();
    replacedInnerCardSurvivesWhileLeased();
    workerRetiresReplacedCardAtTurnEnd();
    rollbackPreservesOldOnAffinityFailure();
    canonicalOwnerTransfer();
    const auto gauge = globalCardLifetimeManager().gauge();
    std::printf("WRAPPED_ADOPTION checks=%d failures=%d destroyed=%llu pending=%llu reservations=%llu leases=%llu managed=%llu\n",
                checks, failures,
                static_cast<unsigned long long>(gauge.actually_destroyed),
                static_cast<unsigned long long>(gauge.pending_delete),
                static_cast<unsigned long long>(gauge.adoption_reserved),
                static_cast<unsigned long long>(gauge.native_leases),
                static_cast<unsigned long long>(gauge.managed_live));
    return failures == 0 ? 0 : 1;
}

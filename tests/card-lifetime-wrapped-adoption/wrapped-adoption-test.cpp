#include "card-lifetime-manager.h"
#include "wrapped-card.h"

#include <QCoreApplication>
#include <QThread>

#include <cstdio>

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
    const auto gauge = manager.gauge();
    check(gauge.adoption_reserved == 0, "owner.reservations-zero");
    check(gauge.native_leases == 0, "owner.leases-zero");
    check(gauge.managed_live == 0, "owner.managed-zero");
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    replacementPreservesIncomingAndRetiresOld();
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

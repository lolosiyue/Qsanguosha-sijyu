#include "card-lifetime-manager.h"
#include "card.h"
#include "structs.h"

#include <QCoreApplication>
#include <QEvent>

#include <cstdio>

namespace {
struct Result {
    int checks = 0;
    int failures = 0;
};

void check(Result &result, bool condition, const char *name)
{
    ++result.checks;
    if (!condition) {
        ++result.failures;
        std::fprintf(stderr, "FAIL %s\n", name);
    }
}

void flushDeferredDeletes()
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

void directCardUseConstruction(Result &result)
{
    CardLifetimeManager &manager = globalCardLifetimeManager();
    manager.resetForTest();
    auto *card = new DummyCard;
    const quint64 destroyedBefore = manager.gauge().actually_destroyed;
    {
        CardUseStruct use(card, nullptr);
        const auto token = manager.observeCard(card);
        check(result, token != nullptr, "use.construct.observed");
        manager.requestNativeDelete(token);
        manager.drain();
        const auto gauge = manager.gauge();
        check(result, manager.gauge().actually_destroyed == destroyedBefore,
              "use.construct.lease_blocks_drain");
        check(result, gauge.pending_delete == 1, "use.construct.pending_is_retained");
    }
    manager.drain();
    flushDeferredDeletes();
    check(result, manager.gauge().actually_destroyed == destroyedBefore + 1,
          "use.construct.release_reclaims_once");
}

void directCardResponseConstruction(Result &result)
{
    CardLifetimeManager &manager = globalCardLifetimeManager();
    manager.resetForTest();
    auto *card = new DummyCard;
    const quint64 destroyedBefore = manager.gauge().actually_destroyed;
    {
        CardResponseStruct response(card, false);
        const auto token = manager.observeCard(card);
        manager.requestNativeDelete(token);
        manager.drain();
        const auto gauge = manager.gauge();
        check(result, manager.gauge().actually_destroyed == destroyedBefore,
              "response.construct.lease_blocks_drain");
        check(result, gauge.pending_delete == 1, "response.construct.pending_is_retained");
    }
    manager.drain();
    flushDeferredDeletes();
    check(result, manager.gauge().actually_destroyed == destroyedBefore + 1,
          "response.construct.release_reclaims_once");
}

void moveTransfersAndEmptiesSource(Result &result)
{
    CardLifetimeManager &manager = globalCardLifetimeManager();
    manager.resetForTest();
    auto *useCard = new DummyCard;
    {
        CardUseStruct source(useCard, nullptr);
        CardUseStruct destination(std::move(source));
        check(result, source.card == nullptr, "use.move.source_card_empty");
        check(result, destination.card == useCard, "use.move.destination_card_transferred");
        const auto token = manager.observeCard(useCard);
        manager.requestNativeDelete(token);
        manager.drain();
        check(result, manager.gauge().pending_delete == 1,
              "use.move.destination_lease_blocks_drain");
    }
    manager.drain();
    flushDeferredDeletes();
    check(result, manager.gauge().actually_destroyed == 1,
          "use.move.destination_release_reclaims_once");

    manager.resetForTest();
    auto *responseCard = new DummyCard;
    {
        CardResponseStruct source(responseCard, false);
        CardResponseStruct destination(std::move(source));
        check(result, source.m_card == nullptr, "response.move.source_card_empty");
        check(result, destination.m_card == responseCard,
              "response.move.destination_card_transferred");
        const auto token = manager.observeCard(responseCard);
        manager.requestNativeDelete(token);
        manager.drain();
        check(result, manager.gauge().pending_delete == 1,
              "response.move.destination_lease_blocks_drain");
    }
    manager.drain();
    flushDeferredDeletes();
    check(result, manager.gauge().actually_destroyed == 1,
          "response.move.destination_release_reclaims_once");
}

void changeRefreshesLeases(Result &result)
{
    CardLifetimeManager &manager = globalCardLifetimeManager();
    manager.resetForTest();
    auto *oldCard = new DummyCard;
    auto *newCard = new DummyCard;
    const auto oldToken = manager.observeCard(oldCard);
    const auto newToken = manager.observeCard(newCard);
    {
        CardUseStruct use(oldCard, nullptr);
        manager.requestNativeDelete(oldToken);
        use.changeCard(newCard);
        manager.drain();
        flushDeferredDeletes();
        check(result, manager.gauge().actually_destroyed == 1,
              "use.change.old_generation_released");
        manager.requestNativeDelete(newToken);
        manager.drain();
        flushDeferredDeletes();
        check(result, manager.gauge().actually_destroyed == 1,
              "use.change.new_generation_retained");
    }
    manager.drain();
    flushDeferredDeletes();
    check(result, manager.gauge().actually_destroyed == 2,
          "use.change.new_generation_released");

    manager.resetForTest();
    oldCard = new DummyCard;
    newCard = new DummyCard;
    const auto oldResponseToken = manager.observeCard(oldCard);
    const auto newResponseToken = manager.observeCard(newCard);
    {
        CardResponseStruct response(oldCard, false);
        manager.requestNativeDelete(oldResponseToken);
        response.changeCard(newCard);
        manager.drain();
        flushDeferredDeletes();
        check(result, manager.gauge().actually_destroyed == 1,
              "response.change.old_generation_released");
        manager.requestNativeDelete(newResponseToken);
        manager.drain();
        flushDeferredDeletes();
        check(result, manager.gauge().actually_destroyed == 1,
              "response.change.new_generation_retained");
    }
    manager.drain();
    flushDeferredDeletes();
    check(result, manager.gauge().actually_destroyed == 2,
          "response.change.new_generation_released");
}

void ownedCardUsesManagedDeleter(Result &result)
{
    CardLifetimeManager &manager = globalCardLifetimeManager();
    manager.resetForTest();
    auto *owned = new DummyCard;
    manager.observeCard(owned, true);
    const quint64 destroyedBefore = manager.gauge().actually_destroyed;
    {
        CardUseStruct use;
        use.setOwnedCard(owned);
    }
    check(result, manager.gauge().actually_destroyed == destroyedBefore,
          "owned_card.deleter_is_deferred_policy");
    flushDeferredDeletes();
    check(result, manager.gauge().actually_destroyed == destroyedBefore + 1,
          "owned_card.deleter_destroys_once");
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Result result;
    directCardUseConstruction(result);
    directCardResponseConstruction(result);
    moveTransfersAndEmptiesSource(result);
    changeRefreshesLeases(result);
    ownedCardUsesManagedDeleter(result);
    const auto gauge = globalCardLifetimeManager().gauge();
    std::printf("CARD_EVENT_LEASE checks=%d failures=%d pending=%llu destroyed=%llu native_leases=%llu\n",
                result.checks, result.failures,
                static_cast<unsigned long long>(gauge.pending_delete),
                static_cast<unsigned long long>(gauge.actually_destroyed),
                static_cast<unsigned long long>(gauge.native_leases));
    return result.failures == 0 ? 0 : 1;
}

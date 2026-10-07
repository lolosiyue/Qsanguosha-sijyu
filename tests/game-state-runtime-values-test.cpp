#include "game-rng.h"
#include "resolution-history.h"

#include <QCoreApplication>
#include <QDebug>

#include <type_traits>
#include <utility>

namespace {

int checks = 0;

#define CHECK(expression) do { \
    ++checks; \
    if (!(expression)) { \
        qCritical() << "CHECK failed at" << __FILE__ << __LINE__ << #expression; \
        return false; \
    } \
} while (false)

bool sameState(const GameRng::State &a, const GameRng::State &b)
{
    return a.seed == b.seed && a.drawCount == b.drawCount && a.algorithm == b.algorithm;
}

bool gameRngPublication()
{
    static_assert(noexcept(std::declval<GameRng &>().swapState(std::declval<GameRng &>())));
    static_assert(!std::is_copy_constructible<GameRng>::value);

    GameRng live;
    live.seed(Q_UINT64_C(0x123456789abcdef0));
    live.bounded(13);
    const GameRng::State expectedTarget = live.state();

    GameRng expected;
    CHECK(expected.restoreState(expectedTarget));
    const quint32 expectedNext = expected.generate();
    const int expectedBounded = expected.bounded(37);

    GameRng prepared;
    const GameRng::State earlier{expectedTarget.seed, 1, expectedTarget.algorithm};
    CHECK(prepared.restoreState(earlier));
    const GameRng::State liveBefore = live.state();
    GameRng *const stableObject = &live;
    live.swapState(prepared);

    CHECK(&live == stableObject);
    CHECK(sameState(live.state(), earlier));
    CHECK(sameState(prepared.state(), liveBefore));

    GameRng expectedEarlier;
    CHECK(expectedEarlier.restoreState(earlier));
    CHECK(live.generate() == expectedEarlier.generate());

    GameRng::State invalid = expectedTarget;
    invalid.algorithm = 999;
    const GameRng::State beforeRejectedRestore = live.state();
    CHECK(!live.restoreState(invalid));
    CHECK(sameState(live.state(), beforeRejectedRestore));

    live.swapState(prepared);
    CHECK(sameState(live.state(), liveBefore));
    CHECK(live.generate() == expectedNext);
    CHECK(live.bounded(37) == expectedBounded);
    return true;
}

bool historyPublication()
{
    static_assert(noexcept(std::declval<ResolutionHistoryService &>().swap(
        std::declval<ResolutionHistoryService &>())));

    ResolutionHistoryService live;
    const qint64 turn = live.beginEvent(QStringLiteral("turn"),
                                        {{QStringLiteral("player"), QStringLiteral("p1")} });
    CHECK(turn != 0);
    CHECK(live.appendFact(turn, QStringLiteral("turn_hp_snapshot"),
                          {{QStringLiteral("player"), QStringLiteral("p1")},
                           {QStringLiteral("hp"), 4}}) != 0);
    live.finishEvent(turn);
    const ResolutionHistorySnapshot target = live.snapshot();
    CHECK(target.isComplete());
    const QVariantMap targetValue = target.serialize();

    ResolutionHistoryService prepared;
    CHECK(prepared.restore(target));
    CHECK(prepared.snapshot().serialize() == targetValue);

    const qint64 later = live.beginEvent(QStringLiteral("damage"));
    CHECK(later != 0);
    live.finishEvent(later);
    const QVariantMap liveBefore = live.snapshot().serialize();
    CHECK(liveBefore != targetValue);

    ResolutionHistoryService *const stableObject = &live;
    live.swap(prepared);
    CHECK(&live == stableObject);
    CHECK(live.snapshot().serialize() == targetValue);
    CHECK(prepared.snapshot().serialize() == liveBefore);

    ResolutionHistorySnapshot invalid;
    const QVariantMap beforeRejectedRestore = prepared.snapshot().serialize();
    CHECK(!prepared.restore(invalid));
    CHECK(prepared.snapshot().serialize() == beforeRejectedRestore);

    {
        ResolutionHistoryContextGuard context(live, turn);
        CHECK(live.hasActiveContext());
        CHECK(!live.snapshot().isComplete());
    }
    CHECK(!live.hasActiveContext());
    CHECK(live.snapshot().isComplete());
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (!gameRngPublication() || !historyPublication())
        return 1;
    qInfo() << "game-state-runtime-values:" << checks << "checks passed";
    return 0;
}

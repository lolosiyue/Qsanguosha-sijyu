#include "engine.h"
#include "distance-skill-cache.h"
#include "room-runtime.h"
#include "runtime-paths.h"
#include "settings.h"
#include "standard.h"
#include "wrapped-card.h"
#include <QCoreApplication>
#include <cstdio>
#include <cstdlib>

static void check(bool ok, const char *what)
{
    if (!ok) { fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}
class ProbeDistance : public DistanceSkill {
public:
    explicit ProbeDistance(const QString &name) : DistanceSkill(name) {}
    int getCorrect(const Player *, const Player *) const override { ++calls; return 1; }
    mutable int calls = 0;
};
class ProbeOther : public MaxCardsSkill {
public:
    explicit ProbeOther(const QString &name) : MaxCardsSkill(name) {}
    int getExtra(const Player *) const override { return 0; }
};

class ProbePlayer : public Player {
public:
    ProbePlayer() : Player(nullptr) {}
    int aliveCount(bool = false) const override { return 4; }
    QString getGameMode() const override { return "04p"; }
    Player *getNextAlive(int = 1) const override { return nullptr; }
    Player *getLastAlive(int = 1) const override { return nullptr; }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    SkillRegistry registry;
    auto *first = new ProbeDistance("h50p_registry");
    quint64 revision = registry.version();
    registry.add(first);
    check(registry.version() > revision, "registration version");
    revision = registry.version();
    auto *second = new ProbeDistance("h50p_registry");
    registry.add(second);
    check(registry.version() > revision && registry.distanceSkills() == QList<const DistanceSkill *>{second}, "replacement version/order");
    revision = registry.version();
    second->setObjectName("h50p_renamed");
    check(registry.version() > revision, "rename version");
    revision = registry.version();
    delete second;
    check(registry.version() > revision && registry.distanceSkills().isEmpty(), "synchronous lifetime version");
    delete first;
    auto *survivor = new ProbeDistance("h50p_survivor");
    { SkillRegistry transient; transient.add(survivor); }
    delete survivor; // destroyed connection must not dereference a dead registry

    // Test the production cache's mid-fill and exact-list guards independently
    // of engine setup; callbacks are not needed to mutate a definition registry.
    DistanceSkillCache cache;
    quint64 generation = 0;
    int fills = 0;
    QSet<QString> exclusions;
    auto stamp = [&] { return DistanceSkillCache::Stamp{generation, 0, exclusions}; };
    auto *a = new ProbeDistance("h50p_guard_a");
    auto *b = new ProbeDistance("h50p_guard_b");
    auto stable = [&] { ++fills; return QList<const DistanceSkill *>{b, a}; };
    cache.get(stamp, [&] { ++fills; ++generation; return QList<const DistanceSkill *>{a}; });
    check(cache.get(stamp, stable) == QList<const DistanceSkill *>({b, a}) && fills == 2, "mid-fill mutation is not cached");
    check(cache.get(stamp, stable) == QList<const DistanceSkill *>({b, a}) && fills == 2, "warm cache preserves exact ordered list");
    exclusions.insert("h50p_guard_a");
    cache.get(stamp, stable);
    check(fills == 3, "exact exclusion set invalidates");
    delete a; // Deliberately no registry stamp change: weak identity guard still rejects.
    check(cache.get(stamp, [&] { ++fills; return QList<const DistanceSkill *>{b}; }) == QList<const DistanceSkill *>{b} && fills == 4, "weak lifetime guard");
    b->setObjectName("h50p_guard_renamed"); // Also deliberately without a stamp bump.
    cache.get(stamp, [&] { ++fills; return QList<const DistanceSkill *>{b}; });
    check(fills == 5, "exact dependency name guard");
    delete b;

    QString error;
    check(QSanRuntimePaths::resolve(app.arguments(), &error), qPrintable(error));
    Engine engine;
    auto *base = new ProbeDistance("h50p_vector");
    engine.addSkills({base});
    check(engine.getDistanceSkills().contains(base), "bootstrap lookup");
    // No initialize(): native definitions and wrapper state suffice; no game/AI VM needed.
    auto *roomA = new RoomRuntime(nullptr);
    auto *roomB = new RoomRuntime(nullptr);
    {
        EngineRuntimeContextScope scope(engine, roomA);
        const auto initial = engine.getDistanceSkills();
        check(initial.contains(base) && engine.getDistanceSkills() == initial, "room bootstrap merge/warm lookup");
        auto *local = new ProbeDistance("h50p_vector");
        revision = roomA->definitions().skillDefinitionVersion();
        engine.addSkills({local});
        check(roomA->definitions().skillDefinitionVersion() > revision, "Engine addSkills routes registry version");
        check(engine.getDistanceSkills().contains(local) && !engine.getDistanceSkills().contains(base), "same-name room replacement");
        auto *replaced = new ProbeDistance("h50p_vector");
        engine.addSkills({replaced});
        check(engine.getDistanceSkills().contains(replaced) && !engine.getDistanceSkills().contains(local), "cached distance-to-distance replacement");
        local = replaced;
        auto *other = new ProbeOther("h50p_vector");
        engine.addSkills({other});
        check(!engine.getDistanceSkills().contains(local) && !engine.getDistanceSkills().contains(base), "non-distance shadowing");
        delete other;
        check(engine.getDistanceSkills().contains(base), "deleted shadow restores bootstrap");
        auto *earlyShadow = new ProbeOther("h50p_vector");
        // This observer is installed BEFORE the registry's lifetime bump.
        // The hidden dependency must still reject the warm cache reentrantly.
        QObject::connect(earlyShadow, &QObject::destroyed, [&] {
            check(engine.getDistanceSkills().contains(base), "shadow deletion before registry observer");
        });
        engine.addSkills({earlyShadow});
        check(!engine.getDistanceSkills().contains(base), "warm hidden shadow cache");
        delete earlyShadow;
        auto *tail = new ProbeDistance("h50p_tail");
        engine.addSkills({tail});
        const auto merged = engine.getDistanceSkills();
        check(merged.indexOf(tail) < merged.indexOf(base), "room skills precede bootstrap in exact order");
        for (int i = 0; i < 2; ++i)
            for (const DistanceSkill *skill : engine.getDistanceSkills())
                if (skill == tail) skill->getCorrect(nullptr, nullptr);
        check(tail->calls == 2, "callbacks run on every vector lookup");
        delete tail;
        check(!engine.getDistanceSkills().contains(tail), "deleted distance definition evicted");

        roomA->state().reset();
        int horseId = -1;
        for (int id = 0; id < engine.getCardCount(); ++id)
            if (qobject_cast<const OffensiveHorse *>(engine.getEngineCard(id))) { horseId = id; break; }
        check(horseId >= 0, "native offensive horse fixture");
        auto *wrapper = qobject_cast<WrappedCard *>(roomA->state().getCard(horseId));
        ProbePlayer player;
        player.setAlive(true);
        player.setEquip(wrapper);
        const QString horseName = wrapper->objectName();
        check(player.hasOffensiveHorse(horseName), "equipped horse initially live");
        revision = roomA->stateRevision();
        auto *replacement = new DefensiveHorse(Card::Spade, 1);
        replacement->setObjectName("h50p_defensive");
        wrapper->takeOver(replacement);
        check(roomA->stateRevision() > revision && !player.hasOffensiveHorse(horseName), "equipped takeover changes live horse and revision");
        revision = roomA->stateRevision();
        wrapper->takeOver(replacement);
        check(roomA->stateRevision() == revision, "same-pointer adoption is a no-op");
        wrapper->takeOver(nullptr);
        check(roomA->stateRevision() == revision, "failed/null adoption is a no-op");
        roomA->state().resetCard(horseId);
        check(roomA->stateRevision() > revision && player.hasOffensiveHorse(horseName), "reset/copyEverythingFrom restores horse and bumps revision");
    }
    {
        EngineRuntimeContextScope scope(engine, roomB);
        check(engine.getDistanceSkills().contains(base), "room isolation");
        auto *clearSkill = new ProbeDistance("h50p_clear");
        engine.addSkills({clearSkill});
        check(engine.getDistanceSkills().contains(clearSkill), "warm room vector before clear");
        revision = roomB->definitions().skillDefinitionVersion();
        roomB->definitions().clear();
        check(roomB->definitions().skillDefinitionVersion() > revision
              && !engine.getDistanceSkills().contains(clearSkill)
              && engine.getDistanceSkills().contains(base), "registry clear/lifetime invalidates cache");
    }
    check(engine.getDistanceSkills().contains(base), "return to bootstrap cache");
    auto *newBase = new ProbeDistance("h50p_vector");
    engine.addSkills({newBase});
    check(engine.getDistanceSkills().contains(newBase) && !engine.getDistanceSkills().contains(base), "bootstrap same-name replacement");
    delete base;
    delete newBase;
    check(!engine.getDistanceSkills().contains(newBase), "bootstrap deletion");
    fprintf(stdout, "PASS H50P2c registry, room merge, lifetime, callbacks, equipped wrapper takeover/reset\n");
    fflush(stdout);
    // These deliberately uninitialized runtimes have no worker shutdown lifecycle.
    // Do not exercise production shutdown with such fixtures.
    std::_Exit(0);
}

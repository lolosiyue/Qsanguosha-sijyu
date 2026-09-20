#include "engine-bootstrap.h"
#include "engine.h"
#include "room.h"
#include "serverplayer.h"
#include "standard.h"
#include "work-scenario.h"

#include <QDebug>
#include <QJsonArray>

struct ScenarioWorkRuntimeTestAccess {
    static void add(Room &room, ServerPlayer *player) { room.addPlayerToRoster(player); }
    static void reorder(Room &room, const QList<ServerPlayer *> &players)
    {
        room.replacePlayerOrder(players);
    }
};

namespace {
using namespace ScenarioWork;
int failures = 0;
void check(bool value, const char *message)
{
    if (!value) {
        ++failures;
        qCritical() << "work runtime test:" << message;
    }
}
void pin(WorkDefinition &work)
{
    for (auto &scene : work.scenes)
        scene.revision = computeSceneRevision(scene);
    for (auto &entry : work.entries)
        for (const auto &scene : work.scenes)
            if (entry.sceneId == scene.id)
                entry.sceneRevision = scene.revision;
    work.revision = computeRevision(work);
}
WorkDefinition fixture(const QJsonObject &compatibility)
{
    auto work = defaultWork();
    work.compatibility = compatibility;
    work.rules = QJsonObject { { "fixedSeats", true }, { "secondGeneral", false } };
    work.scenes.first().setup = QStringLiteral("general:sujiang role:rebel maxhp:4 hp:3 draw:0 starter:true\n"
                                               "general:sujiang role:lord maxhp:4 hp:4 draw:0\n");
    work.scenes.first().playerSeat = 0;
    pin(work);
    return work;
}
QSharedPointer<WorkLaunch> launchFor(const WorkDefinition &work)
{
    auto launch = QSharedPointer<WorkLaunch>::create();
    launch->work = work;
    launch->entryId = work.entries.first().id;
    launch->runId = QStringLiteral("runtime-fixture");
    return launch;
}
void admission(const QJsonObject &compatibility)
{
    auto work = fixture(compatibility);
    check(QSanWorks::WorkScenario(launchFor(work)).isValid(), "baseline pinned work is admitted");
    auto missingFingerprint = work;
    missingFingerprint.compatibility = { };
    pin(missingFingerprint);
    check(!QSanWorks::WorkScenario(launchFor(missingFingerprint)).isValid(),
        "missing runtime fingerprint rejects");
    auto changedAfterPin = work;
    changedAfterPin.title += QStringLiteral(" changed");
    check(!QSanWorks::WorkScenario(launchFor(changedAfterPin)).isValid(),
        "runtime requires exact work revision");
    auto mismatch = work;
    mismatch.compatibility["cards"] = "different-card-order-same-count";
    pin(mismatch);
    check(!QSanWorks::WorkScenario(launchFor(mismatch)).isValid(), "ordered card mismatch rejects");
    auto unpinned = launchFor(work);
    unpinned->work.entries.first().sceneRevision = "stale-pin";
    unpinned->work.revision = computeRevision(unpinned->work);
    check(!QSanWorks::WorkScenario(unpinned).isValid(), "entry cannot launch a different scene revision");
    auto missing = work;
    missing.scenes.first().setup.replace("general:sujiang", "general:missing_work_test_general");
    pin(missing);
    check(!QSanWorks::WorkScenario(launchFor(missing)).isValid(), "unknown scene general rejects");
    if (Sanguosha->getCardCount() > 0) {
        auto duplicate = work;
        duplicate.scenes.first().setup.replace("draw:0", "draw:0 hand:0");
        pin(duplicate);
        check(!QSanWorks::WorkScenario(launchFor(duplicate)).isValid(),
            "two seats cannot own one physical card");
    }
    auto carried = launchFor(work);
    carried->carry.values["hp"] = 2;
    check(!QSanWorks::WorkScenario(carried).isValid(), "carry cannot override a non-whitelisted stat");
    carried->work.carry.hp = true;
    pin(carried->work);
    check(QSanWorks::WorkScenario(carried).isValid(), "whitelisted positive hp is admitted");
    carried->carry.values["hp"] = 5;
    check(!QSanWorks::WorkScenario(carried).isValid(),
        "carry hp over explicit next maxhp rejects instead of clamping");
    auto derivedMaximum = QSharedPointer<WorkLaunch>::create(*carried);
    derivedMaximum->work.scenes.first().setup.remove("maxhp:4 ");
    pin(derivedMaximum->work);
    derivedMaximum->carry.values["hp"] = 999;
    check(!QSanWorks::WorkScenario(derivedMaximum).isValid(),
        "carry hp is checked against general-derived maxhp");
    auto reducedMaximum = QSharedPointer<WorkLaunch>::create(*carried);
    reducedMaximum->work.carry.maxhp = true;
    reducedMaximum->carry.values = QJsonObject { { "maxhp", 2 } };
    pin(reducedMaximum->work);
    check(
        !QSanWorks::WorkScenario(reducedMaximum).isValid(), "carried maxhp cannot invalidate next scene hp");
    carried->carry.values["hp"] = 0;
    check(!QSanWorks::WorkScenario(carried).isValid(), "dead hp cannot become a continuation");
    carried->carry.values = QJsonObject { { "general", "missing_work_test_general" } };
    carried->work.carry.generals = true;
    pin(carried->work);
    check(!QSanWorks::WorkScenario(carried).isValid(), "unknown carried general rejects");
    for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
        if (!qobject_cast<const EquipCard *>(Sanguosha->getCard(id)))
            continue;
        auto equipment = launchFor(work);
        equipment->work.carry.equip = true;
        equipment->work.scenes.first().setup.replace(
            "starter:true", "starter:true equipArea:0*0,1*0,2*0,3*0,4*0");
        pin(equipment->work);
        equipment->carry.values["equip"] = QJsonArray { id };
        check(!QSanWorks::WorkScenario(equipment).isValid(), "carried equipment cannot fill disabled slots");
        auto baselineEquipment = equipment->work;
        baselineEquipment.scenes.first().setup.replace(
            "starter:true", QStringLiteral("starter:true equip:%1").arg(id));
        pin(baselineEquipment);
        check(!QSanWorks::WorkScenario(launchFor(baselineEquipment)).isValid(),
            "baseline equipment also respects disabled slots");
        break;
    }
}
void stableSeatsAndCapture(const QJsonObject &compatibility)
{
    auto work = fixture(compatibility);
    work.carry.hp = true;
    work.carry.maxhp = true;
    work.carry.marks << QStringLiteral("work_fixture_mark");
    GoalDefinition goal;
    goal.mode = GoalMode::Objective;
    goal.success.predicates << GoalPredicate { PredicateType::Hp, 0, PredicateOp::Le, 2, QString() };
    goal.failure.predicates << GoalPredicate { PredicateType::Mark, 0, PredicateOp::Ge, 1,
        QStringLiteral("work_fixture_mark") };
    work.scenes.first().goals = { goal };
    pin(work);
    GameSessionConfig config;
    config.workLaunch = launchFor(work);
    // Deferred rooms exercise ownership/seat snapshots without starting Lua,
    // a worker thread, network requests, or a complete match.
    Room first(nullptr, QStringLiteral("02p"), config, Room::RuntimeInitializationPolicy::Deferred);
    Room second(nullptr, QStringLiteral("02p"), config, Room::RuntimeInitializationPolicy::Deferred);
    const auto *scenario = dynamic_cast<const QSanWorks::WorkScenario *>(first.getScenario());
    check(scenario && first.workError().isEmpty() && second.workError().isEmpty(), "both rooms admit work");
    if (!scenario)
        return;
    check(first.getScenario() != second.getScenario(), "rooms own separate scenario rules");
    auto *human = new ServerPlayer(&first);
    auto *enemy = new ServerPlayer(&first);
    human->setObjectName("work_human");
    enemy->setObjectName("work_enemy");
    human->setRole("rebel");
    enemy->setRole("lord");
    human->setMaxHp(4);
    human->setHp(2);
    human->setAlive(true);
    enemy->setMaxHp(4);
    enemy->setHp(4);
    enemy->setAlive(true);
    ScenarioWorkRuntimeTestAccess::add(first, human);
    ScenarioWorkRuntimeTestAccess::add(first, enemy);
    scenario->bindPlayers(&first);
    first.adjustSeats();
    check(first.getPlayers().first() == human, "work roster does not rotate non-lord human away");
    ScenarioWorkRuntimeTestAccess::reorder(first, { enemy, human });
    check(scenario->players(&first).first() == human, "declared seat survives later reorder");
    const auto captured = scenario->captureCarry(&first);
    human->setHp(1);
    check(captured.values.value("hp").toInt() == 2, "captured continuation is detached from later cleanup");
    human->setMark("work_fixture_mark", 1);
    const auto outcome = scenario->evaluate(&first);
    check(!outcome.success && outcome.failure,
        "simultaneous predicates settle as failure and cannot report success");
    human->setAlive(false);
    check(scenario->captureCarry(&first).values.isEmpty(), "dead human cannot produce carry state");
    check(!second.getTag("WorkSetupComplete").toBool(), "work run state is not shared across rooms");
}
}

int runScenarioWorkRuntimeTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "work runtime engine bootstrap failed:" << error;
        return 1;
    }
    failures = 0;
    const auto compatibility = QSanWorks::currentCompatibility();
    admission(compatibility);
    stableSeatsAndCapture(compatibility);
    return failures == 0 ? 0 : 2;
}

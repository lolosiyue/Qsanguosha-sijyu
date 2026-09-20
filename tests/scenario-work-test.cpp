#include "scenario-work.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

using namespace ScenarioWork;

namespace {
int failures = 0;
#define CHECK_SCENARIO(condition)                                                                            \
    do {                                                                                                     \
        if (!(condition)) {                                                                                  \
            qWarning() << __FILE__ << __LINE__ << #condition;                                                \
            ++failures;                                                                                      \
        }                                                                                                    \
    } while (false)

WorkDefinition stageWork()
{
    WorkDefinition work = defaultWork();
    work.kind = WorkKind::Stage;
    work.selection = SelectionPolicy::Sequential;
    SceneDefinition second = draftScene();
    second.title = QStringLiteral("Second");
    second.revision = computeSceneRevision(second);
    work.scenes.append(second);
    work.entries.first().id = QStringLiteral("first");
    work.entries.first().sceneRevision = work.scenes.first().revision;
    work.entries.append(
        { QStringLiteral("second"), second.id, second.revision, second.title, QString(), { } });
    work.revision = computeRevision(work);
    return work;
}

void testJsonAndValidation()
{
    WorkDefinition work = defaultWork();
    work.revision = computeRevision(work);
    QStringList errors;
    CHECK_SCENARIO(validateWork(work, &errors));

    QJsonObject malformed = workToJson(work);
    malformed.insert(QStringLiteral("kind"), 7);
    WorkDefinition loaded;
    QString parseError;
    CHECK_SCENARIO(!workFromJson(malformed, &loaded, &parseError));
    malformed = workToJson(work);
    malformed.insert(QStringLiteral("selection"), QStringLiteral("random"));
    CHECK_SCENARIO(!workFromJson(malformed, &loaded, &parseError));

    auto scene = work.scenes.first();
    scene.goals.clear();
    GoalDefinition objective;
    objective.mode = GoalMode::Objective;
    scene.goals.append(objective);
    work.scenes[0] = scene;
    CHECK_SCENARIO(!validateWork(work, &errors));

    GoalDefinition settlement;
    settlement.mode = GoalMode::Settlement;
    CHECK_SCENARIO(evaluateGoals(settlement, { }, 0).success);
    CHECK_SCENARIO(!evaluateGoals(settlement, { }, 0).failure);
    GoalDefinition objectiveGoal;
    objectiveGoal.mode = GoalMode::Objective;
    CHECK_SCENARIO(!evaluateGoals(objectiveGoal, { }, 0).success);
    GoalPredicate alive;
    alive.type = PredicateType::Alive;
    alive.seat = 0;
    objectiveGoal.success.predicates << alive;
    GoalPredicate dead = alive;
    dead.type = PredicateType::Dead;
    objectiveGoal.failure.predicates << dead;
    CHECK_SCENARIO(evaluateGoals(objectiveGoal, { SeatState { true, 3, { } } }, 0).success);
    CHECK_SCENARIO(!evaluateGoals(objectiveGoal, { SeatState { true, 3, { } } }, 0).failure);
}

void testCarryAndLegacyShapes()
{
    CarryState carry;
    carry.values.insert(QStringLiteral("script"), QStringLiteral("return 1"));
    CHECK_SCENARIO(!validateCarryState(carry));
    carry.values = QJsonObject { { QStringLiteral("hand"), QJsonArray { 1, 2 } },
        { QStringLiteral("general"), QStringLiteral("lord") },
        { QStringLiteral("marks"),
            QJsonArray { QJsonObject {
                { QStringLiteral("name"), QStringLiteral("x") }, { QStringLiteral("value"), 1 } } } } };
    CHECK_SCENARIO(validateCarryState(carry));

    LegacySceneDocument document;
    QString error;
    CHECK_SCENARIO(parseLegacyScene(
        QStringLiteral("general:a role:lord starter:true\n general:b role:rebel\n"), &document, &error));
    CHECK_SCENARIO(!parseLegacyScene(QStringLiteral("general:a role:lord endedByPile:x "
                                                    "singleTurn:y\n general:b role:rebel\n"),
        &document, &error));
    CHECK_SCENARIO(!parseLegacyScene(
        QStringLiteral("general:a role:lord card:5\n general:b role:rebel card:5\n"), &document, &error));
}

void testProgressAndSelection()
{
    QTemporaryDir directory;
    CHECK_SCENARIO(directory.isValid());
    WorkDefinition work = stageWork();
    WorkProgress progress;
    QString error;
    CHECK_SCENARIO(loadProgress(directory.path(), work, &progress, &error));
    CHECK_SCENARIO(progress.completedEntryIds.isEmpty());
    CHECK_SCENARIO(canPlayEntry(work, progress, QStringLiteral("first")));
    CHECK_SCENARIO(!canPlayEntry(work, progress, QStringLiteral("second")));

    StageRunResult result;
    result.runId = QStringLiteral("run-1");
    result.workId = work.id;
    result.revision = work.revision;
    result.entryId = QStringLiteral("first");
    result.success = true;
    result.carry.values = QJsonObject { { QStringLiteral("hp"), 2 } };
    CHECK_SCENARIO(recordResult(directory.path(), work, result, &progress, &error));
    CHECK_SCENARIO(progress.completedEntryIds == QStringList { QStringLiteral("first") });
    CHECK_SCENARIO(progress.continuationEntryId == QStringLiteral("second"));
    CHECK_SCENARIO(progress.snapshots.size() == 1);
    CHECK_SCENARIO(progress.snapshots.first().entryId == QStringLiteral("second"));
    CHECK_SCENARIO(recordResult(directory.path(), work, result, &progress, &error));
    CHECK_SCENARIO(progress.snapshots.size() == 1);
    CHECK_SCENARIO(canPlayEntry(work, progress, QStringLiteral("second")));

    StageRunResult wrong = result;
    wrong.runId = QStringLiteral("run-2");
    wrong.revision = QStringLiteral("wrong");
    CHECK_SCENARIO(!recordResult(directory.path(), work, wrong, nullptr, &error));
    wrong.revision = work.revision;
    wrong.aborted = true;
    wrong.success = false;
    CHECK_SCENARIO(recordResult(directory.path(), work, wrong, &progress, &error));
    CHECK_SCENARIO(!progress.processedRunIds.contains(QStringLiteral("run-2")));

    StageRunResult trial = result;
    trial.runId = QStringLiteral("trial-1");
    trial.trial = true;
    CHECK_SCENARIO(recordResult(directory.path(), work, trial, &progress, &error));
    CHECK_SCENARIO(!progress.processedRunIds.contains(QStringLiteral("trial-1")));

    WorkProgress loaded;
    CHECK_SCENARIO(loadProgress(directory.path(), work, &loaded, &error));
    CHECK_SCENARIO(loaded.snapshots.size() == 1);
    WorkDefinition freeWork = work;
    freeWork.selection = SelectionPolicy::Free;
    CHECK_SCENARIO(canPlayEntry(freeWork, loaded, QStringLiteral("second")));

    StageRunResult unknown = result;
    unknown.runId = QStringLiteral("unknown-entry");
    unknown.entryId = QStringLiteral("missing");
    CHECK_SCENARIO(!recordResult(directory.path(), work, unknown, nullptr, &error));
}
void testImmutableRevisionsAndStrictImport()
{
    WorkDefinition work = defaultWork();
    const auto firstHash = work.scenes.first().revision;
    CHECK_SCENARIO(computeSceneRevision(work.scenes.first()) == firstHash);
    auto edited = work.scenes.first();
    edited.title = "Changed";
    edited.revision = computeSceneRevision(edited);
    CHECK_SCENARIO(edited.revision != firstHash);
    work.scenes.append(edited);
    CHECK_SCENARIO(validateWork(work)); // Same identity, different pinned revisions.
    work.entries.first().sceneRevision = edited.revision;
    CHECK_SCENARIO(validateWork(work));
    work.revision = computeRevision(work);
    CHECK_SCENARIO(computeRevision(work) == work.revision);
    WorkDefinition loaded;
    CHECK_SCENARIO(workFromJson(workToJson(work), &loaded));
    CHECK_SCENARIO(loaded.scenes.size() == 2);
    CHECK_SCENARIO(loaded.entries.first().sceneRevision == edited.revision);
    work.entries.first().sceneRevision = QString(64, '0');
    CHECK_SCENARIO(!validateWork(work));
    work.entries.first().sceneRevision = edited.revision;
    work.scenes.append(edited);
    CHECK_SCENARIO(!validateWork(work));
    work.scenes.removeLast();

    const auto original = workToJson(loaded);
    for (const auto &badValue : QJsonArray { 1.5, "1", true }) {
        auto malformed = original;
        malformed.insert("schemaVersion", badValue);
        CHECK_SCENARIO(!workFromJson(malformed, &loaded));
        CHECK_SCENARIO(workToJson(loaded) == original);
    }
    auto malformed = original;
    malformed.insert("progress", QJsonObject { });
    CHECK_SCENARIO(!workFromJson(malformed, &loaded));
    malformed = original;
    auto scenes = malformed.value("scenes").toArray();
    auto scene = scenes.first().toObject();
    scene.insert("playerSeat", 0.5);
    scenes[0] = scene;
    malformed.insert("scenes", scenes);
    CHECK_SCENARIO(!workFromJson(malformed, &loaded));
    malformed = original;
    auto carry = malformed.value("carry").toObject();
    carry.insert("hp", 1);
    malformed.insert("carry", carry);
    CHECK_SCENARIO(!workFromJson(malformed, &loaded));
    CHECK_SCENARIO(workToJson(loaded) == original);

    QTemporaryDir directory;
    CHECK_SCENARIO(writeWork(directory.path(), loaded));
    WorkDefinition disk;
    CHECK_SCENARIO(readWork(workFilePath(directory.path(), loaded), &disk));
    CHECK_SCENARIO(workToJson(disk) == original);
    disk.id = "CON";
    CHECK_SCENARIO(workFilePath(directory.path(), disk).isEmpty());
    disk.id = "../outside";
    CHECK_SCENARIO(!writeWork(directory.path(), disk));
    CHECK_SCENARIO(listWorks(directory.path()).size() == 1);
    malformed = original;
    malformed.insert("title", "tampered");
    const auto external = directory.filePath("tampered.qswork.json");
    QFile file(external);
    CHECK_SCENARIO(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(malformed).toJson());
    file.close();
    CHECK_SCENARIO(!readWork(external, &loaded));
    CHECK_SCENARIO(workToJson(loaded) == original);
}

void testLegacyRoundTripAndBounds()
{
    const QString valid = "setPile:1,2,3\ngeneral:a role:lord starter:true hp:3 maxhp:4 hujia:0 marks:x*2 "
                          "equipArea:0*1,1*0 futureField:retained hand:4\ngeneral:b role:rebel judge:5\n";
    LegacySceneDocument parsed, again;
    CHECK_SCENARIO(parseLegacyScene(valid, &parsed));
    CHECK_SCENARIO(parseLegacyScene(serializeLegacyScene(parsed), &again));
    CHECK_SCENARIO((again.fixedPile == QList<int> { 1, 2, 3 }));
    CHECK_SCENARIO(again.players == parsed.players);
    CHECK_SCENARIO(serializeLegacyScene(again) == serializeLegacyScene(parsed));
    for (const auto &replacement : QStringList { "hp:0", "hp:1000", "hp:1.5" })
        CHECK_SCENARIO(!parseLegacyScene(QString(valid).replace("hp:3", replacement), &again));
    CHECK_SCENARIO(!parseLegacyScene(QString(valid).replace("hand:4", "hand:2"), &again));
    CHECK_SCENARIO(!parseLegacyScene(QString(valid).replace("role:rebel", "role:lord"), &again));
    CHECK_SCENARIO(!parseLegacyScene(QString(valid).replace("role:rebel", "role:loyalist"), &again));
    CHECK_SCENARIO(!parseLegacyScene(QString(valid).replace("starter:true", "starter:yes"), &again));
    CHECK_SCENARIO(!parseLegacyScene(QString(valid).replace("0*1,1*0", "0*100"), &again));
    CHECK_SCENARIO(!parseLegacyScene(QString(valid).replace("marks:x*2", "marks:x*1.5"), &again));
    CHECK_SCENARIO(!parseLegacyScene(QString(valid).replace("hp:3", "hp:3 draw:1.5"), &again));
    CHECK_SCENARIO(!parseLegacyScene(QString(valid).replace("hp:3", "hp:3 hpadj:-1000"), &again));
    CHECK_SCENARIO(!parseLegacyScene(QString(valid).replace("hp:3", "hp:3 turned:yes"), &again));
    CHECK_SCENARIO(again.players == parsed.players); // Failure leaves output untouched.
    CarryState carry;
    carry.values = { { "hp", 1.5 } };
    CHECK_SCENARIO(!validateCarryState(carry));
    carry.values = { { "hand", QJsonArray { 1, 1 } } };
    CHECK_SCENARIO(!validateCarryState(carry));
    carry.values = { { "marks", QJsonArray { QJsonObject { { "name", "x" }, { "value", 1.5 } } } } };
    CHECK_SCENARIO(!validateCarryState(carry));
}

void testGoalsAndDurableProgress()
{
    GoalDefinition goal;
    goal.mode = GoalMode::Objective;
    goal.success.all = false;
    goal.success.predicates = { { PredicateType::Hp, 0, PredicateOp::Ge, 3, { } },
        { PredicateType::Mark, 0, PredicateOp::Eq, 2, "x" } };
    goal.failure.predicates = { { PredicateType::Turns, 0, PredicateOp::Ge, 2, { } } };
    QList<SeatState> seats { { true, 1, { { "x", 2 } } } };
    CHECK_SCENARIO(evaluateGoals(goal, seats, 1).success);
    CHECK_SCENARIO(evaluateGoals(goal, seats, 2).failure);
    CHECK_SCENARIO(!evaluateGoals(goal, seats, 2).success); // Failure wins ties.
    goal.success.all = true;
    CHECK_SCENARIO(!evaluateGoals(goal, seats, 1).success);

    auto conflict = defaultWork();
    conflict.scenes.first().setup.prepend("extraOptions:beforeStartRound:2 beforeStartRoundWinner:lord\n");
    conflict.scenes.first().revision = computeSceneRevision(conflict.scenes.first());
    conflict.entries.first().sceneRevision = conflict.scenes.first().revision;
    CHECK_SCENARIO(validateWork(conflict));
    conflict.entries.first().goals.append(goal);
    CHECK_SCENARIO(!validateWork(conflict));

    QTemporaryDir directory;
    auto work = stageWork();
    WorkProgress progress;
    StageRunResult run;
    run.workId = work.id;
    run.revision = work.revision;
    run.entryId = "first";
    run.runId = "attempt-1";
    run.success = false;
    CHECK_SCENARIO(recordResult(directory.path(), work, run, &progress));
    CHECK_SCENARIO(QDir(directory.path()).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
    run.success = true;
    run.trial = true;
    CHECK_SCENARIO(recordResult(directory.path(), work, run));
    run.trial = false;
    run.aborted = true;
    CHECK_SCENARIO(recordResult(directory.path(), work, run));
    CHECK_SCENARIO(QDir(directory.path()).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
    run.aborted = false;
    run.entryId = "second";
    CHECK_SCENARIO(!recordResult(directory.path(), work, run));
    run.entryId = "first";
    run.carry.values = { { "hp", 2 } };
    CHECK_SCENARIO(recordResult(directory.path(), work, run, &progress));
    CHECK_SCENARIO(progress.snapshots.size() == 1);
    auto changed = progress;
    changed.snapshots.first().carry.values = { { "hp", 3 } };
    CHECK_SCENARIO(!saveProgress(directory.path(), changed));
    run.runId = "attempt-2";
    run.carry.values = { { "hp", 3 } };
    CHECK_SCENARIO(recordResult(directory.path(), work, run, &progress));
    CHECK_SCENARIO(progress.snapshots.size() == 2);
    CHECK_SCENARIO(progress.snapshots.first().carry.values.value("hp").toInt() == 2);
    run.runId = "attempt-3";
    run.entryId = "second";
    CHECK_SCENARIO(recordResult(directory.path(), work, run, &progress));
    CHECK_SCENARIO(progress.continuationEntryId.isEmpty());
    CHECK_SCENARIO(progress.snapshots.size() == 2);
    CHECK_SCENARIO(recordResult(directory.path(), work, run, &progress));
    CHECK_SCENARIO(progress.processedRunIds.size() == 3);
    CHECK_SCENARIO(loadProgress(directory.path(), work, &changed));
    CHECK_SCENARIO(changed.continuationEntryId.isEmpty());
    changed.revision = QString(64, '0');
    CHECK_SCENARIO(!canPlayEntry(work, changed, "first"));
}

} // namespace

int runScenarioWorkTests()
{
    failures = 0;
    testJsonAndValidation();
    testCarryAndLegacyShapes();
    testProgressAndSelection();
    testImmutableRevisionsAndStrictImport();
    testLegacyRoundTripAndBounds();
    testGoalsAndDurableProgress();
    return failures == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    return runScenarioWorkTests();
}

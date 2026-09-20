#include "scenario-work-examples.h"
#include "card.h"
#include "engine.h"
#include <QCoreApplication>

namespace {
QString text(const char *source) { return QCoreApplication::translate("ScenarioWorkExamples", source); }

int cardId(const QString &name)
{
    for (int i = 0; i < Sanguosha->getCardCount(); ++i) {
        const Card *card = Sanguosha->getEngineCard(i);
        if (card && card->objectName() == name)
            return i;
    }
    return -1;
}

ScenarioWork::GoalDefinition objective(ScenarioWork::PredicateType type, int seat,
    ScenarioWork::PredicateOp op = ScenarioWork::PredicateOp::Eq, int threshold = 0)
{
    ScenarioWork::GoalDefinition goal;
    goal.mode = ScenarioWork::GoalMode::Objective;
    ScenarioWork::GoalPredicate predicate;
    predicate.type = type;
    predicate.seat = seat;
    predicate.op = op;
    predicate.threshold = threshold;
    goal.success.predicates << predicate;
    ScenarioWork::GoalPredicate failure;
    failure.type = ScenarioWork::PredicateType::Dead;
    failure.seat = 0;
    goal.failure.predicates << failure;
    return goal;
}

ScenarioWork::SceneDefinition scene(const QString &id, const QString &title, const QString &opening,
    const QString &setup, const ScenarioWork::GoalDefinition &goal)
{
    auto result = ScenarioWork::draftScene();
    result.id = id;
    result.title = title;
    result.author = QStringLiteral("QSanguosha");
    result.intro = opening;
    result.opening = opening;
    result.ending = text(QT_TRANSLATE_NOOP(
        "ScenarioWorkExamples", "Objective completed. You can replay this scene from its original setup."));
    result.setup = setup;
    result.goals = { goal };
    result.revision = ScenarioWork::computeSceneRevision(result);
    return result;
}

ScenarioWork::WorkDefinition work(
    const QString &id, const QJsonObject &compatibility, const QList<ScenarioWork::SceneDefinition> &scenes)
{
    auto result = ScenarioWork::defaultWork();
    result.id = id;
    result.title = scenes.first().title;
    result.author = QStringLiteral("QSanguosha");
    result.intro = scenes.first().intro;
    result.compatibility = compatibility;
    result.rules = { { QStringLiteral("secondGeneral"), false } };
    result.kind = scenes.size() > 1 ? ScenarioWork::WorkKind::Stage : ScenarioWork::WorkKind::Scene;
    result.scenes = scenes;
    result.entries.clear();
    for (int i = 0; i < scenes.size(); ++i) {
        ScenarioWork::StageEntry entry;
        entry.id = QStringLiteral("entry-%1").arg(i + 1);
        entry.sceneId = scenes[i].id;
        entry.sceneRevision = scenes[i].revision;
        entry.title = scenes[i].title;
        result.entries << entry;
    }
    result.revision = ScenarioWork::computeRevision(result);
    return result;
}
}

QList<ScenarioWork::WorkDefinition> scenarioWorkExamples(const QJsonObject &compatibility)
{
    using namespace ScenarioWork;
    QList<WorkDefinition> works;
    const int peach = cardId(QStringLiteral("peach"));
    const int slash = cardId(QStringLiteral("slash"));
    if (peach >= 0) {
        const auto lesson = scene(QStringLiteral("example-heal-scene"),
            text(QT_TRANSLATE_NOOP("ScenarioWorkExamples", "Tutorial: Recover health")),
            text(QT_TRANSLATE_NOOP("ScenarioWorkExamples",
                "Use the Peach in your hand to reach 2 health. The scene ends after the effect resolves.")),
            QStringLiteral("general:sujiang role:lord starter:true maxhp:3 hp:1 draw:0 hand:%1\n"
                           "general:sujiang role:rebel maxhp:4 hp:4 draw:0\n")
                .arg(peach),
            objective(PredicateType::Hp, 0, PredicateOp::Ge, 2));
        works << work(QStringLiteral("example-heal"), compatibility, { lesson });
    }
    if (slash >= 0) {
        auto goal = objective(PredicateType::Dead, 1);
        goal.failure.all = false;
        GoalPredicate timeout;
        timeout.type = PredicateType::Turns;
        timeout.op = PredicateOp::Ge;
        timeout.threshold = 1;
        goal.failure.predicates << timeout;
        const auto puzzle = scene(QStringLiteral("example-slash-scene"),
            text(QT_TRANSLATE_NOOP("ScenarioWorkExamples", "Puzzle: One turn")),
            text(QT_TRANSLATE_NOOP(
                "ScenarioWorkExamples", "Defeat the opponent with your Slash before your first turn ends.")),
            QStringLiteral("general:sujiang role:lord starter:true maxhp:3 hp:3 draw:0 hand:%1\n"
                           "general:sujiang role:rebel maxhp:1 hp:1 draw:0\n")
                .arg(slash),
            goal);
        works << work(QStringLiteral("example-puzzle"), compatibility, { puzzle });
        if (peach >= 0) {
            auto first = works.first().scenes.first();
            first.id = QStringLiteral("example-story-heal");
            first.title = text(QT_TRANSLATE_NOOP("ScenarioWorkExamples", "Chapter 1: Rest before departure"));
            first.revision = computeSceneRevision(first);
            auto second = puzzle;
            second.id = QStringLiteral("example-story-gate");
            second.title = text(QT_TRANSLATE_NOOP("ScenarioWorkExamples", "Chapter 2: The gatekeeper"));
            second.opening = text(QT_TRANSLATE_NOOP("ScenarioWorkExamples",
                "Your health and general carry over from the previous chapter. Defeat the gatekeeper."));
            second.revision = computeSceneRevision(second);
            auto story = work(QStringLiteral("example-story"), compatibility, { first, second });
            story.title = text(QT_TRANSLATE_NOOP("ScenarioWorkExamples", "Short story: A journey begins"));
            story.selection = SelectionPolicy::Free;
            story.carry.hp = story.carry.maxhp = story.carry.generals = true;
            story.revision = computeRevision(story);
            works << story;
        }
    }
    if (Sanguosha->getSkill(QStringLiteral("kurou"))) {
        const auto regression = scene(QStringLiteral("example-kurou-scene"),
            text(QT_TRANSLATE_NOOP("ScenarioWorkExamples", "Skill case: Kurou")),
            text(QT_TRANSLATE_NOOP("ScenarioWorkExamples",
                "Use Kurou. Check its health cost and draw effect; the objective checks health at effect "
                "completion.")),
            QStringLiteral("general:sujiang role:lord starter:true maxhp:3 hp:3 draw:0 acquireSkills:kurou\n"
                           "general:sujiang role:rebel maxhp:4 hp:4 draw:0\n"),
            objective(PredicateType::Hp, 0, PredicateOp::Le, 2));
        works << work(QStringLiteral("example-kurou"), compatibility, { regression });
    }
    return works;
}

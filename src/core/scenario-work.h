#ifndef QSAN_SCENARIO_WORK_H
#define QSAN_SCENARIO_WORK_H

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QMetaType>
#include <QString>
#include <QStringList>

namespace ScenarioWork {

enum class WorkKind { Scene, Stage };
enum class SelectionPolicy { Sequential, Free };
enum class GoalMode { Objective, Settlement };
enum class PredicateType { Alive, Dead, Hp, Mark, Turns };
enum class PredicateOp { Lt, Le, Eq, Ge, Gt };

struct GoalPredicate {
    PredicateType type = PredicateType::Alive;
    int seat = 0;
    PredicateOp op = PredicateOp::Eq;
    int threshold = 0;
    QString mark;
};

struct GoalGroup {
    bool all = true;
    QList<GoalPredicate> predicates;
};

struct GoalDefinition {
    GoalMode mode = GoalMode::Settlement;
    GoalGroup success;
    GoalGroup failure;
};

struct SceneDefinition {
    QString id;
    QString revision;
    QString title;
    QString author;
    QString intro;
    QString opening;
    QString ending;
    QString setup; // Legacy mini-scene text.
    int playerSeat = 0;
    QList<GoalDefinition> goals;
};

struct StageEntry {
    QString id;
    QString sceneId;
    QString sceneRevision;
    QString title;
    QString intro;
    QList<GoalDefinition> goals;
};

struct CarryState {
    QJsonObject values;
};

bool validateCarryState(const CarryState &state, QStringList *errors = nullptr);

struct CarryPolicy {
    bool hp = false;
    bool maxhp = false;
    bool hujia = false;
    bool generals = false;
    bool hand = false;
    bool equip = false;
    QStringList marks;
    QStringList skills;
    bool enabled() const;
};

struct WorkDefinition {
    int schemaVersion = 1;
    WorkKind kind = WorkKind::Scene;
    QString id;
    QString revision;
    QString title;
    QString author;
    QString intro;
    QString rule = QStringLiteral("mini_identity");
    QJsonObject compatibility;
    QJsonObject rules;
    SelectionPolicy selection = SelectionPolicy::Sequential;
    QList<SceneDefinition> scenes;
    QList<StageEntry> entries;
    CarryPolicy carry;
    QJsonObject legacyFields;
};

struct WorkLaunch {
    WorkDefinition work;
    QString entryId;
    CarryState carry;
    QString runId;
    bool trial = false;
};

struct StageRunResult {
    QString runId;
    QString workId;
    QString revision;
    QString entryId;
    bool success = false;
    bool aborted = false;
    bool trial = false;
    QString reason;
    CarryState carry;
};
struct SeatState {
    bool alive = false;
    int hp = 0;
    QMap<QString, int> marks;
};

struct GoalEvaluation {
    bool success = false;
    bool failure = false;
};

GoalEvaluation evaluateGoals(
    const GoalDefinition &goal, const QList<SeatState> &seats, int completedPlayerTurns);

struct ProgressSnapshot {
    QString id;
    QString sourceEntryId;
    QString entryId;
    QDateTime createdAt;
    CarryState carry;
};

struct WorkProgress {
    QString workId;
    QString revision;
    QStringList completedEntryIds;
    QString continuationEntryId;
    QList<ProgressSnapshot> snapshots;
    QStringList processedRunIds;
};

struct WorkFileInfo {
    QString id;
    QString revision;
    QString title;
    WorkKind kind = WorkKind::Scene;
    QString path;
};

WorkDefinition defaultWork();
SceneDefinition draftScene();

QJsonObject workToJson(const WorkDefinition &work);
bool workFromJson(const QJsonObject &json, WorkDefinition *work, QString *error = nullptr);
bool validateWork(const WorkDefinition &work, QStringList *errors = nullptr);

QString computeRevision(const WorkDefinition &work);
QString computeSceneRevision(const SceneDefinition &scene);
QString workFilePath(const QString &libraryRoot, const WorkDefinition &work);
bool readWork(const QString &path, WorkDefinition *work, QString *error = nullptr);
bool writeWork(const QString &libraryRoot, const WorkDefinition &work, QString *error = nullptr);
QList<WorkFileInfo> listWorks(const QString &libraryRoot, QString *error = nullptr);

bool loadProgress(
    const QString &libraryRoot, const WorkDefinition &work, WorkProgress *progress, QString *error = nullptr);
bool canPlayEntry(const WorkDefinition &work, const WorkProgress &progress, const QString &entryId);
bool saveProgress(const QString &libraryRoot, const WorkProgress &progress, QString *error = nullptr);
bool recordResult(const QString &libraryRoot, const WorkDefinition &work, const StageRunResult &result,
    WorkProgress *progress = nullptr, QString *error = nullptr);

struct LegacySceneDocument {
    QStringList extraOptions;
    QList<int> fixedPile;
    QList<QMap<QString, QString>> players;
    QMap<QString, QString> fields;
};

bool parseLegacyScene(const QString &text, LegacySceneDocument *document, QString *error = nullptr);
QString serializeLegacyScene(const LegacySceneDocument &document);
bool validateLegacySceneShape(const LegacySceneDocument &document, QStringList *errors = nullptr);

} // namespace ScenarioWork

Q_DECLARE_METATYPE(ScenarioWork::StageRunResult)
Q_DECLARE_METATYPE(ScenarioWork::WorkLaunch)

#endif

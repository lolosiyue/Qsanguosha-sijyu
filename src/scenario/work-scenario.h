#ifndef QSAN_WORK_SCENARIO_H
#define QSAN_WORK_SCENARIO_H

#include "miniscenarios.h"
#include "scenario-work.h"

#include <QJsonObject>
#include <QSharedPointer>

class Room;
class ServerPlayer;
class General;

namespace QSanWorks {

// Runtime admission checks shared by server and non-GUI launchers.
QJsonObject currentCompatibility();
bool validateWorkForRuntime(const ScenarioWork::WorkDefinition &work, QString *error = nullptr);
int sceneGeneralHealth(const General *primary, const General *secondary, bool welfare, bool maximum);

class WorkScenario final : public MiniScene {
public:
    explicit WorkScenario(const QSharedPointer<const ScenarioWork::WorkLaunch> &launch);

    bool isValid() const { return m_error.isEmpty(); }
    QString error() const { return m_error; }
    QString entryId() const { return m_entryId; }
    int playerSeat() const { return m_playerSeat; }
    const ScenarioWork::GoalDefinition *goal() const { return m_hasGoal ? &m_goal : nullptr; }
    const ScenarioWork::WorkLaunch &launch() const { return *m_launch; }
    void bindPlayers(Room *room) const;
    QList<ServerPlayer *> players(Room *room) const;
    ScenarioWork::GoalEvaluation evaluate(Room *room) const;
    ScenarioWork::CarryState captureCarry(Room *room) const;

private:
    QSharedPointer<const ScenarioWork::WorkLaunch> m_launch;
    QString m_entryId;
    int m_playerSeat = 0;
    QString m_error;
    ScenarioWork::GoalDefinition m_goal;
    bool m_hasGoal = false;
};

} // namespace QSanWorks

#endif

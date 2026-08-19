#include "test-suite.h"

#include <QCoreApplication>

int runRoomNotifierTests();
int runSkillRuntimeCoordinatorTests();
int runRequestCoordinatorTests();
int runCardMovementServiceTests();
int runExtraTurnSchedulerTests();
int runRoomRosterTests();
int runPlayerLifecycleServiceTests(int argc, char **argv);
int runPlayerDecisionServiceTests();

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QString suite = parseSuite(argc, argv);
    if (suite == QLatin1String("room-notifier"))
        return runRoomNotifierTests();
    if (suite == QLatin1String("skill-runtime"))
        return runSkillRuntimeCoordinatorTests();
    if (suite == QLatin1String("request"))
        return runRequestCoordinatorTests();
    if (suite == QLatin1String("card-movement"))
        return runCardMovementServiceTests();
    if (suite == QLatin1String("extra-turn"))
        return runExtraTurnSchedulerTests();
    if (suite == QLatin1String("room-roster"))
        return runRoomRosterTests();
    if (suite == QLatin1String("player-lifecycle"))
        return runPlayerLifecycleServiceTests(argc, argv);
    if (suite == QLatin1String("player-decision"))
        return runPlayerDecisionServiceTests();
    return 64;
}

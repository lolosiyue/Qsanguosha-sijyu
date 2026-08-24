#include "test-suite.h"

#include <QCoreApplication>

#ifdef _WIN32
#include <crtdbg.h>
#include <cstdlib>
#include <windows.h>
#endif

static void configureNonInteractiveErrors()
{
#ifdef _WIN32
    SetErrorMode(SetErrorMode(0) | SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    for (const int reportType : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(reportType, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(reportType, _CRTDBG_FILE_STDERR);
    }
#endif
}

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
    configureNonInteractiveErrors();
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

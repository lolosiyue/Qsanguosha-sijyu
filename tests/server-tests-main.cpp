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
int runPreGameLockTests();
int runOutboundOrderingTests();
int runCardMovementServiceTests();
int runExtraTurnSchedulerTests();
int runRoomRosterTests();
int runPlayerLifecycleServiceTests(int argc, char **argv);
int runPlayerDecisionServiceTests();

static int runSelectedSuite(const QString &suite, int argc, char **argv)
{
    if (suite == QLatin1String("room-notifier"))
        return runRoomNotifierTests();
    if (suite == QLatin1String("skill-runtime"))
        return runSkillRuntimeCoordinatorTests();
    if (suite == QLatin1String("request"))
        return runRequestCoordinatorTests();
    if (suite == QLatin1String("pre-game-lock"))
        return runPreGameLockTests();
    if (suite == QLatin1String("outbound-ordering"))
        return runOutboundOrderingTests();
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

int main(int argc, char **argv)
{
    configureNonInteractiveErrors();
    QCoreApplication application(argc, argv);
    const QString suite = parseSuite(argc, argv);
    if (!suite.isEmpty())
        return runSelectedSuite(suite, argc, argv);

    return runIsolatedTestCases("SERVER_UNIT_RESULT", {
        {QStringLiteral("room-notifier"), {QStringLiteral("--suite"), QStringLiteral("room-notifier")}},
        {QStringLiteral("skill-runtime"), {QStringLiteral("--suite"), QStringLiteral("skill-runtime")}},
        {QStringLiteral("request"), {QStringLiteral("--suite"), QStringLiteral("request")}},
        {QStringLiteral("pre-game-lock"), {QStringLiteral("--suite"), QStringLiteral("pre-game-lock")}},
        {QStringLiteral("outbound-ordering"), {QStringLiteral("--suite"), QStringLiteral("outbound-ordering")}},
        {QStringLiteral("card-movement"), {QStringLiteral("--suite"), QStringLiteral("card-movement")}},
        {QStringLiteral("extra-turn"), {QStringLiteral("--suite"), QStringLiteral("extra-turn")}},
        {QStringLiteral("room-roster"), {QStringLiteral("--suite"), QStringLiteral("room-roster")}},
        {QStringLiteral("player-lifecycle"), {QStringLiteral("--suite"), QStringLiteral("player-lifecycle")}},
        {QStringLiteral("player-decision"), {QStringLiteral("--suite"), QStringLiteral("player-decision")}, 900000}
    });
}

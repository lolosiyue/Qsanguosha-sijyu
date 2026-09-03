#include "test-suite.h"

#include <QCoreApplication>
#include <QString>

#include <cstdio>

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

static bool parseSyntheticSeed(int argc, char **argv, quint64 &seed)
{
    bool foundSeed = false;
    for (int i = 1; i < argc; ++i) {
        if (QLatin1String(argv[i]) != QLatin1String("--seed"))
            continue;
        if (foundSeed) {
            std::fprintf(stderr, "invalid --seed: duplicate option\n");
            return false;
        }
        foundSeed = true;
        if (i + 1 >= argc) {
            std::fprintf(stderr, "invalid --seed: value is required\n");
            return false;
        }
        const QString token = QString::fromLatin1(argv[i + 1]);
        if (token.isEmpty()) {
            std::fprintf(stderr, "invalid --seed: value is required\n");
            return false;
        }
        for (const QChar character : token) {
            if (character < QLatin1Char('0') || character > QLatin1Char('9')) {
                const QByteArray value = token.toLatin1();
                std::fprintf(stderr, "invalid --seed value: %s\n", value.constData());
                return false;
            }
        }
        bool ok = false;
        seed = token.toULongLong(&ok, 10);
        if (!ok) {
            const QByteArray value = token.toLatin1();
            std::fprintf(stderr, "invalid --seed value: %s\n", value.constData());
            return false;
        }
        ++i;
    }
    if (!foundSeed)
        seed = Q_UINT64_C(2026082201);
    return true;
}

int runLuaRuntimeIsolationTests();
int runRoomRuntimeIsolationTests();
int runRoomRuntimeLuaTeardownTests();
int runCardLifetimeTests();
int runCardLifetimeSyntheticTests(int actorCount, quint64 seed);
int runCardLifetimeLegacyRedTests();
int runCardLifetimeRoomStateTests();
int runCardLifetimeLuaTests();
int runCardLifetimeDerivedCardConversionTests();
int runCardLifetimeEventLeaseFixture(int argc, char **argv);
int runCardLifetimeWrappedAdoptionFixture(int argc, char **argv);
int runCardLifetimeShutdownFixture(int argc, char **argv);

int main(int argc, char **argv)
{
    configureNonInteractiveErrors();
    const QString earlySuite = parseSuite(argc, argv);
    if (earlySuite == QLatin1String("card-lifetime-event-lease"))
        return runCardLifetimeEventLeaseFixture(argc, argv);
    if (earlySuite == QLatin1String("card-lifetime-wrapped-adoption"))
        return runCardLifetimeWrappedAdoptionFixture(argc, argv);
    if (earlySuite == QLatin1String("card-lifetime-shutdown")) {
        if (argc > 3) {
            const QString requestedCase = QString::fromLocal8Bit(argv[3]);
            if (requestedCase == QLatin1String("worker")
                || requestedCase == QLatin1String("lease")
                || requestedCase == QLatin1String("reservation")
                || requestedCase == QLatin1String("lua-pin")
                || requestedCase == QLatin1String("overlap")) {
                char *fixtureArgv[] = {argv[0], argv[3], nullptr};
                return runCardLifetimeShutdownFixture(2, fixtureArgv);
            }
        }
        return runCardLifetimeShutdownFixture(argc, argv);
    }
    QCoreApplication application(argc, argv);
    const QString suite = parseSuite(argc, argv);
    if (suite == QLatin1String("runtime-contract")) {
        quint64 seed = 0;
        if (!parseSyntheticSeed(argc, argv, seed))
            return 64;
        return runIsolatedTestCases("RUNTIME_CONTRACT_RESULT", {
            {QStringLiteral("lua-runtime"), {QStringLiteral("--suite"), QStringLiteral("lua-runtime")}},
            {QStringLiteral("room-runtime"), {QStringLiteral("--suite"), QStringLiteral("room-runtime")}},
            {QStringLiteral("room-lua-teardown"), {QStringLiteral("--suite"), QStringLiteral("room-lua-teardown")}},
            {QStringLiteral("card-lifetime"), {QStringLiteral("--suite"), QStringLiteral("card-lifetime")}, 600000},
            {QStringLiteral("card-lifetime-lua"), {QStringLiteral("--suite"), QStringLiteral("card-lifetime-lua")}},
            {QStringLiteral("synthetic-30"), {
                QStringLiteral("--suite"), QStringLiteral("card-lifetime-synthetic-30"),
                QStringLiteral("--seed"), QString::number(seed)}}
        });
    }
    if (suite == QLatin1String("lua-runtime"))
        return runLuaRuntimeIsolationTests();
    if (suite == QLatin1String("room-runtime"))
        return runRoomRuntimeIsolationTests();
    if (suite == QLatin1String("room-lua-teardown"))
        return runRoomRuntimeLuaTeardownTests();
    if (suite == QLatin1String("card-lifetime"))
    {
        return runCardLifetimeTests();
    }
    if (suite == QLatin1String("card-lifetime-lua"))
        return runCardLifetimeLuaTests();
    if (suite == QLatin1String("card-lifetime-derived-card-red"))
        return runCardLifetimeDerivedCardConversionTests();
    if (suite == QLatin1String("card-lifetime-synthetic-30")
        || suite == QLatin1String("card-lifetime-synthetic-50")) {
        quint64 seed = 0;
        if (!parseSyntheticSeed(argc, argv, seed))
            return 64;
        const int actorCount = suite == QLatin1String("card-lifetime-synthetic-30") ? 30 : 50;
        return runCardLifetimeSyntheticTests(actorCount, seed);
    }
    if (suite == QLatin1String("card-lifetime-legacy-red"))
        return runCardLifetimeLegacyRedTests();
    if (suite == QLatin1String("card-lifetime-roomstate"))
    {
        return runCardLifetimeRoomStateTests();
    }
    return 64;
}

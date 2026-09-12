// The four GUI contract tests (M1 startup / M2 network / M2B-A multimedia /
// M2B-B effects) share one executable. They all link only Qt6::Core -- the
// contracts must not require QApplication, Qt Multimedia, OpenGL, or art
// assets to be verified -- so merging them into one target keeps a
// server-only configure working; it just removes three Visual Studio projects.
//
// Each suite still runs in its own process (runIsolatedTestCases re-execs the
// executable), so one suite's global state and Qt lifecycle never leak into
// the next.
#include "test-suite.h"

#include <QCoreApplication>

int runUiStartupSmokeReportTests(int argc, char *argv[]);
int runNetworkUiSmokeReportTests(int argc, char *argv[]);
int runMultimediaSmokeReportTests(int argc, char *argv[]);
int runEffectsProfileTests(int argc, char **argv);

int main(int argc, char **argv)
{
    // Each sub-suite creates its own QCoreApplication, so this path must not create one first.
    const QString suite = parseSuite(argc, argv);
    if (suite == QLatin1String("startup-smoke-report"))
        return runUiStartupSmokeReportTests(argc, argv);
    if (suite == QLatin1String("network-smoke-report"))
        return runNetworkUiSmokeReportTests(argc, argv);
    if (suite == QLatin1String("multimedia-report"))
        return runMultimediaSmokeReportTests(argc, argv);
    if (suite == QLatin1String("effects-profile"))
        return runEffectsProfileTests(argc, argv);
    if (!suite.isEmpty())
        return 64;

    QCoreApplication application(argc, argv);
    return runIsolatedTestCases("UI_CONTRACT_RESULT", {
        {QStringLiteral("startup-smoke-report"),
         {QStringLiteral("--suite"), QStringLiteral("startup-smoke-report")}},
        {QStringLiteral("network-smoke-report"),
         {QStringLiteral("--suite"), QStringLiteral("network-smoke-report")}},
        {QStringLiteral("multimedia-report"),
         {QStringLiteral("--suite"), QStringLiteral("multimedia-report")}}
    });
}

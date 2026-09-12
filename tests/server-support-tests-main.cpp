// Shared executable for the tests around the dedicated server (command line,
// logger, real TCP integration). None of the three suites links the engine --
// only Qt6::Core/Qt6::Network plus qsanguosha_protocol_v2_contract_support --
// so merging them changes no suite's link surface. Each suite still runs in
// its own process.
#include "test-suite.h"

#include <QCoreApplication>

#include <vector>

int runServerCommandLineTests(int argc, char **argv);
int runServerLoggerTests(int argc, char **argv);
int runServerNetworkIntegrationTests(int argc, char **argv);

// --suite belongs to the dispatcher itself and must not leak through to the
// suite: the server-command-line-test parser fails immediately on unknown
// arguments.
static int stripSuiteArgument(int argc, char **argv, std::vector<char *> &filtered)
{
    filtered.clear();
    for (int i = 0; i < argc; ++i) {
        if (i > 0 && QLatin1String(argv[i]) == QLatin1String("--suite") && i + 1 < argc) {
            ++i;
            continue;
        }
        filtered.push_back(argv[i]);
    }
    filtered.push_back(nullptr);
    return static_cast<int>(filtered.size()) - 1;
}

int main(int argc, char **argv)
{
    // Each sub-suite creates its own QCoreApplication.
    const QString suite = parseSuite(argc, argv);
    std::vector<char *> filtered;
    const int filteredArgc = stripSuiteArgument(argc, argv, filtered);
    if (suite == QLatin1String("server-cli"))
        return runServerCommandLineTests(filteredArgc, filtered.data());
    if (suite == QLatin1String("server-logging"))
        return runServerLoggerTests(filteredArgc, filtered.data());
    if (suite == QLatin1String("network-integration"))
        return runServerNetworkIntegrationTests(filteredArgc, filtered.data());
    if (!suite.isEmpty())
        return 64;

    QCoreApplication application(argc, argv);
    return runIsolatedTestCases("SERVER_SUPPORT_RESULT", {
        {QStringLiteral("server-logging"),
         {QStringLiteral("--suite"), QStringLiteral("server-logging")}}
    });
}

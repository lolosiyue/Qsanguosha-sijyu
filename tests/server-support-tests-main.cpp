// dedicated server 周邊(command line、logger、真 TCP 整合)嘅測試共用執行檔。
// 三個 suite 都唔 link engine,淨係 Qt6::Core／Qt6::Network 加
// qsanguosha_protocol_v2_contract_support,所以夾埋唔會改變任何一個嘅 link 面。
// 每個 suite 依然行喺自己嘅 process。
#include "test-suite.h"

#include <QCoreApplication>

#include <vector>

int runServerCommandLineTests(int argc, char **argv);
int runServerLoggerTests(int argc, char **argv);
int runServerNetworkIntegrationTests(int argc, char **argv);

// --suite 係 dispatcher 自己嘅參數,唔可以漏落去 suite 本身:
// server-command-line-test 嘅 parser 遇到未知參數係即刻 fail 嘅。
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
    // 子 suite 各自會起自己嘅 QCoreApplication。
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

// TUI 嘅四個測試共用執行檔。佢哋嘅 link 面本身就係同一個超集
// (client_core + tui_support + engine + protocol v2 support + Core/Network),
// 分開只係多三個 Visual Studio project。每個 suite 依然行喺自己嘅 process。
#include "test-suite.h"

#include <QCoreApplication>

int runTuiCardTextTests(int argc, char **argv);
int runTuiClientPlayerTests(int argc, char **argv);
int runTuiLogTextTests(int argc, char **argv);
int runTuiContractTests(int argc, char *argv[]);
int runTuiLiveTcpTests(int argc, char *argv[]);

int main(int argc, char **argv)
{
    // 子 suite 各自會起自己嘅 QCoreApplication。
    const QString suite = parseSuite(argc, argv);
    if (suite == QLatin1String("card-text"))
        return runTuiCardTextTests(argc, argv);
    if (suite == QLatin1String("client-player"))
        return runTuiClientPlayerTests(argc, argv);
    if (suite == QLatin1String("log-text"))
        return runTuiLogTextTests(argc, argv);
    if (suite == QLatin1String("contract"))
        return runTuiContractTests(argc, argv);
    if (suite == QLatin1String("live-tcp"))
        return runTuiLiveTcpTests(argc, argv);
    if (!suite.isEmpty())
        return 64;

    QCoreApplication application(argc, argv);
    return runIsolatedTestCases("TUI_CONTRACT_RESULT", {
        {QStringLiteral("card-text"), {QStringLiteral("--suite"), QStringLiteral("card-text")}},
        {QStringLiteral("client-player"),
            {QStringLiteral("--suite"), QStringLiteral("client-player")}},
        {QStringLiteral("log-text"), {QStringLiteral("--suite"), QStringLiteral("log-text")}},
        {QStringLiteral("contract"), {QStringLiteral("--suite"), QStringLiteral("contract")}},
        {QStringLiteral("live-tcp"), {QStringLiteral("--suite"), QStringLiteral("live-tcp")}}
    });
}

// 四個 GUI 契約測試(M1 startup / M2 network / M2B-A multimedia / M2B-B effects)
// 共用一個執行檔。佢哋全部只 link Qt6::Core —— 契約唔應該要開 QApplication、
// Qt Multimedia、OpenGL 或者美術資產先驗到 —— 所以夾埋一個 target 唔會令
// server-only configure 跑唔到,只係少咗三個 Visual Studio project。
//
// 每個 suite 仍然行喺自己嘅 process(runIsolatedTestCases 會 re-exec 自己),
// 所以一個 suite 嘅全域狀態同 Qt lifecycle 唔會漏去下一個。
#include "test-suite.h"

#include <QCoreApplication>

int runUiStartupSmokeReportTests(int argc, char *argv[]);
int runNetworkUiSmokeReportTests(int argc, char *argv[]);
int runMultimediaSmokeReportTests(int argc, char *argv[]);
int runEffectsProfileTests(int argc, char **argv);

int main(int argc, char **argv)
{
    // 子 suite 各自會起自己嘅 QCoreApplication,所以呢條路唔可以先起一個。
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

#include "test-suite.h"

#include <QCoreApplication>

int runEngineSmokeTests();
int runLuaCompatibilityTests();
int runEngineSelfBridgeTests();
int runCardParseTests();
int runCardMoveReasonTests();
int runEnumReflectionTests();
int runPackagePolicyTests();
int runMigratedGeneralPackageTests();
// 以下 suite 本身係獨立 test 檔嘅 main(),由 CMake 用 COMPILE_DEFINITIONS
// main=... 改名夾埋入嚟,所以簽名保持同原本一模一樣。
int runCardOverviewClassifierTests(int argc, char **argv);
int runCardOverviewModelTests(int argc, char **argv);
int runLocalResponseUiCaseTests(int argc, char **argv);
int runRuntimePathsTests(int argc, char **argv);
int runReplayGameStateTests(int argc, char *argv[]);
int runTakeoverSnapshotTests(int argc, char **argv);
int runPhotoLayoutFitTests();

int main(int argc, char **argv)
{
    // 夾埋入嚟嘅 suite 各自會起自己嘅 QCoreApplication,所以要喺起 application
    // 之前就交俾佢哋。
    {
        const QString merged = parseSuite(argc, argv);
        if (merged == QLatin1String("card-overview-classifier"))
            return runCardOverviewClassifierTests(argc, argv);
        if (merged == QLatin1String("card-overview-model"))
            return runCardOverviewModelTests(argc, argv);
        if (merged == QLatin1String("local-response-case-parser"))
            return runLocalResponseUiCaseTests(argc, argv);
        if (merged == QLatin1String("runtime-paths"))
            return runRuntimePathsTests(argc, argv);
        if (merged == QLatin1String("replay-game-state"))
            return runReplayGameStateTests(argc, argv);
        if (merged == QLatin1String("takeover-snapshot"))
            return runTakeoverSnapshotTests(argc, argv);
        if (merged == QLatin1String("photo-layout-fit"))
            return runPhotoLayoutFitTests();
    }

    QCoreApplication application(argc, argv);
    const QString suite = parseSuite(argc, argv);

    const auto runAll = []() {
        const int luaCompatibility = runLuaCompatibilityTests();
        if (luaCompatibility != 0)
            return 130 + luaCompatibility;
        const int packagePolicy = runPackagePolicyTests();
        if (packagePolicy != 0)
            return 140 + packagePolicy;
        const int smoke = runEngineSmokeTests();
        if (smoke != 0)
            return smoke;
        const int bridge = runEngineSelfBridgeTests();
        if (bridge != 0)
            return bridge;
        const int cardParse = runCardParseTests();
        if (cardParse != 0)
            return cardParse;
        const int cardMoveReason = runCardMoveReasonTests();
        if (cardMoveReason != 0)
            return 150 + cardMoveReason;
        return runEnumReflectionTests();
    };

    if (suite.isEmpty() || suite == QLatin1String("engine-smoke"))
        return runAll();
    if (suite == QLatin1String("self-bridge"))
        return runEngineSelfBridgeTests();
    if (suite == QLatin1String("card-parse"))
        return runCardParseTests();
    if (suite == QLatin1String("card-move-reason"))
        return runCardMoveReasonTests();
    if (suite == QLatin1String("enum-reflection"))
        return runEnumReflectionTests();
    if (suite == QLatin1String("lua-compat"))
        return runLuaCompatibilityTests();
    if (suite == QLatin1String("package-policy"))
        return runPackagePolicyTests();
    if (suite == QLatin1String("package-ownership"))
        return runMigratedGeneralPackageTests();
    return 64;
}

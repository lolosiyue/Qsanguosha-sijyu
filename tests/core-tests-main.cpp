#include "test-suite.h"

#include <QCoreApplication>

int runEngineSmokeTests();
int runScenarioWorkRuntimeTests();
int runLargeRoomModeTests();
int runSkillDescriptionTests();
int runLuaCompatibilityTests();
int runEngineSelfBridgeTests();
int runCardParseTests();
int runCardMoveReasonTests();
int runHiddenPhysicalCardTests();
int runClientTargetEvaluatorTests();
int runEnumReflectionTests();
int runPackagePolicyTests();
int runMigratedGeneralPackageTests();
int runEquipsNullifiedTests();
int runSkillCardServerSelfTests();
int runUserNameResolutionTests();
// The suites below are the main() functions of standalone test files, merged
// in by CMake via COMPILE_DEFINITIONS main=... renaming, so the signatures
// stay identical to the originals.
int runCardOverviewClassifierTests(int argc, char **argv);
int runCardOverviewModelTests(int argc, char **argv);
int runLocalResponseUiCaseTests(int argc, char **argv);
int runRuntimePathsTests(int argc, char **argv);
int runReplayGameStateTests(int argc, char *argv[]);
int runTakeoverSnapshotTests(int argc, char **argv);
int runPhotoLayoutFitTests();
int runRoomLayoutEngineTests();

int main(int argc, char **argv)
{
    // Each merged-in suite creates its own QCoreApplication, so hand control
    // to the suites before creating the application here.
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
        if (merged == QLatin1String("room-layout-engine"))
            return runRoomLayoutEngineTests();
    }

    QCoreApplication application(argc, argv);
    const QString suite = parseSuite(argc, argv);

    if (suite == QLatin1String("scenario-work-runtime"))
        return runScenarioWorkRuntimeTests();

    const auto runAll = []() {
        const int descriptions = runSkillDescriptionTests();
        if (descriptions != 0)
            return 190 + descriptions;
        const int hiddenCards = runHiddenPhysicalCardTests();
        if (hiddenCards != 0)
            return hiddenCards;
        const int luaCompatibility = runLuaCompatibilityTests();
        if (luaCompatibility != 0)
            return 130 + luaCompatibility;
        const int packagePolicy = runPackagePolicyTests();
        if (packagePolicy != 0)
            return 140 + packagePolicy;
        const int smoke = runEngineSmokeTests();
        if (smoke != 0)
            return smoke;
        const int largeRoomMode = runLargeRoomModeTests();
        if (largeRoomMode != 0)
            return 190 + largeRoomMode;
        const int bridge = runEngineSelfBridgeTests();
        if (bridge != 0)
            return bridge;
        const int cardParse = runCardParseTests();
        if (cardParse != 0)
            return cardParse;
        const int cardMoveReason = runCardMoveReasonTests();
        if (cardMoveReason != 0)
            return 150 + cardMoveReason;
        const int targetEvaluator = runClientTargetEvaluatorTests();
        if (targetEvaluator != 0)
            return 160 + targetEvaluator;
        const int equipsNullified = runEquipsNullifiedTests();
        if (equipsNullified != 0)
            return 170 + equipsNullified;
        const int serverSelfCards = runSkillCardServerSelfTests();
        if (serverSelfCards != 0)
            return 180 + serverSelfCards;
        return runEnumReflectionTests();
    };

    if (suite.isEmpty() || suite == QLatin1String("engine-smoke"))
        return runAll();
    if (suite == QLatin1String("large-room-mode"))
        return runLargeRoomModeTests();
    if (suite == QLatin1String("skill-description"))
        return runSkillDescriptionTests();
    if (suite == QLatin1String("hidden-physical-cards"))
        return runHiddenPhysicalCardTests();
    if (suite == QLatin1String("equips-nullified"))
        return runEquipsNullifiedTests();
    if (suite == QLatin1String("skill-card-server-self"))
        return runSkillCardServerSelfTests();
    if (suite == QLatin1String("self-bridge"))
        return runEngineSelfBridgeTests();
    if (suite == QLatin1String("card-parse"))
        return runCardParseTests();
    if (suite == QLatin1String("card-move-reason"))
        return runCardMoveReasonTests();
    if (suite == QLatin1String("client-target-evaluator"))
        return runClientTargetEvaluatorTests();
    if (suite == QLatin1String("enum-reflection"))
        return runEnumReflectionTests();
    if (suite == QLatin1String("lua-compat"))
        return runLuaCompatibilityTests();
    if (suite == QLatin1String("package-policy"))
        return runPackagePolicyTests();
    if (suite == QLatin1String("package-ownership"))
        return runMigratedGeneralPackageTests();
    if (suite == QLatin1String("user-name"))
        return runUserNameResolutionTests();
    return 64;
}

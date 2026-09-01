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

int main(int argc, char **argv)
{
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

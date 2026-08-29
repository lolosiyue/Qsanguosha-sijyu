#include "test-suite.h"

#include <QCoreApplication>

int runEngineSmokeTests();
int runEngineSelfBridgeTests();
int runCardParseTests();
int runEnumReflectionTests();

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QString suite = parseSuite(argc, argv);

    const auto runAll = []() {
        const int smoke = runEngineSmokeTests();
        if (smoke != 0)
            return smoke;
        const int bridge = runEngineSelfBridgeTests();
        if (bridge != 0)
            return bridge;
        const int cardParse = runCardParseTests();
        if (cardParse != 0)
            return cardParse;
        return runEnumReflectionTests();
    };

    if (suite.isEmpty() || suite == QLatin1String("engine-smoke"))
        return runAll();
    if (suite == QLatin1String("self-bridge"))
        return runEngineSelfBridgeTests();
    if (suite == QLatin1String("card-parse"))
        return runCardParseTests();
    if (suite == QLatin1String("enum-reflection"))
        return runEnumReflectionTests();
    return 64;
}

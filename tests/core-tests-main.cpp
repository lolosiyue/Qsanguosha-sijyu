#include "test-suite.h"

#include <QCoreApplication>

int runEngineSmokeTests();
int runEngineSelfBridgeTests();
int runCardParseTests();

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
        return runCardParseTests();
    };

    if (suite.isEmpty() || suite == QLatin1String("engine-smoke"))
        return runAll();
    if (suite == QLatin1String("self-bridge"))
        return runEngineSelfBridgeTests();
    if (suite == QLatin1String("card-parse"))
        return runCardParseTests();
    return 64;
}

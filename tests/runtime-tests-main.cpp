#include "test-suite.h"

#include <QCoreApplication>

int runLuaRuntimeIsolationTests();
int runRoomRuntimeIsolationTests();

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QString suite = parseSuite(argc, argv);
    if (suite == QLatin1String("lua-runtime"))
        return runLuaRuntimeIsolationTests();
    if (suite == QLatin1String("room-runtime"))
        return runRoomRuntimeIsolationTests();
    return 64;
}

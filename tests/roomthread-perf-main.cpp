#include <QCoreApplication>
#include <QString>

#include <cstdio>

int runRoomThreadPerfTests();
int runRoomThreadDeferredStateTests();

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (argc == 1)
        return runRoomThreadPerfTests();
    if (argc == 3 && QString::fromLatin1(argv[1]) == QStringLiteral("--suite")
        && QString::fromLatin1(argv[2]) == QStringLiteral("roomthread-deferred-state"))
        return runRoomThreadDeferredStateTests();
    std::fprintf(stderr,
        "usage: qsanguosha_roomthread_perf_tests "
        "[--suite roomthread-deferred-state]\n");
    return 64;
}

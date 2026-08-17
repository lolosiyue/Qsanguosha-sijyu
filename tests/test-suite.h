#ifndef QSAN_TEST_SUITE_H
#define QSAN_TEST_SUITE_H

#include <QString>

inline QString parseSuite(int argc, char **argv)
{
    for (int i = 1; i + 1 < argc; ++i) {
        if (QLatin1String(argv[i]) == QLatin1String("--suite"))
            return QString::fromLatin1(argv[i + 1]);
    }
    return QString();
}

#endif

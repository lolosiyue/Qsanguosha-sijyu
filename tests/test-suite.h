#ifndef QSAN_TEST_SUITE_H
#define QSAN_TEST_SUITE_H

#include <QCoreApplication>
#include <QList>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <cstdio>

inline QString parseSuite(int argc, char **argv)
{
    for (int i = 1; i + 1 < argc; ++i) {
        if (QLatin1String(argv[i]) == QLatin1String("--suite"))
            return QString::fromLatin1(argv[i + 1]);
    }
    return QString();
}

struct IsolatedTestCase
{
    QString name;
    QStringList arguments;
    int timeoutMs = 300000;
};

inline void writeChildOutput(FILE *stream, const QByteArray &output)
{
    if (output.isEmpty())
        return;
    std::fwrite(output.constData(), 1, static_cast<size_t>(output.size()), stream);
    if (!output.endsWith('\n'))
        std::fputc('\n', stream);
}

// Consolidated CTest entries retain the old per-suite process boundary. This
// prevents one suite's globals or Qt lifecycle from leaking into the next one.
inline int runIsolatedTestCases(const char *summaryName,
    const QList<IsolatedTestCase> &testCases)
{
    QList<bool> results;
    QStringList details;
    int passedCount = 0;

    for (const IsolatedTestCase &testCase : testCases) {
        QProcess process;
        process.setProcessChannelMode(QProcess::SeparateChannels);
        process.start(QCoreApplication::applicationFilePath(), testCase.arguments);

        bool passed = false;
        QString detail;
        if (!process.waitForStarted(30000)) {
            detail = QStringLiteral("could not start: %1").arg(process.errorString());
        } else if (!process.waitForFinished(testCase.timeoutMs)) {
            process.terminate();
            if (!process.waitForFinished(5000)) {
                process.kill();
                process.waitForFinished(5000);
            }
            detail = QStringLiteral("timed out after %1 ms").arg(testCase.timeoutMs);
        } else if (process.exitStatus() != QProcess::NormalExit) {
            detail = QStringLiteral("child process crashed");
        } else if (process.exitCode() != 0) {
            detail = QStringLiteral("exit code %1").arg(process.exitCode());
        } else {
            passed = true;
            ++passedCount;
        }

        writeChildOutput(stdout, process.readAllStandardOutput());
        writeChildOutput(stderr, process.readAllStandardError());
        const QByteArray name = testCase.name.toUtf8();
        if (passed) {
            std::fprintf(stdout, "[PASS] %s\n", name.constData());
        } else {
            const QByteArray detailText = detail.toUtf8();
            std::fprintf(stderr, "[FAIL] %s: %s\n", name.constData(), detailText.constData());
        }
        results.append(passed);
        details.append(detail);
        std::fflush(stdout);
        std::fflush(stderr);
    }

    std::fprintf(stdout, "\n%s\n\n", summaryName);
    for (int i = 0; i < testCases.size(); ++i) {
        const QByteArray name = testCases.at(i).name.toUtf8();
        if (results.at(i)) {
            std::fprintf(stdout, "PASS %s\n", name.constData());
        } else {
            const QByteArray detail = details.at(i).toUtf8();
            std::fprintf(stdout, "FAIL %s: %s\n", name.constData(), detail.constData());
        }
    }
    const int totalCount = static_cast<int>(testCases.size());
    std::fprintf(stdout, "\nTOTAL: %d\nPASS: %d\nFAIL: %d\n",
        totalCount, passedCount, totalCount - passedCount);
    std::fflush(stdout);
    return passedCount == testCases.size() ? 0 : 1;
}

#endif

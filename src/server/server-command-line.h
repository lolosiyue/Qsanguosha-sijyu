#ifndef QSAN_SERVER_COMMAND_LINE_H
#define QSAN_SERVER_COMMAND_LINE_H

#include <QString>
#include <QStringList>

#include <optional>

struct ServerCommandLineOptions
{
    bool helpRequested = false;
    bool versionRequested = false;
    bool listGameModes = false;
    bool printConfig = false;
    bool checkConfig = false;
    bool jsonOutput = false;
    std::optional<quint16> port;
    std::optional<int> operationTimeout;
    std::optional<int> aiDelay;
    std::optional<bool> aiEnabled;
    std::optional<quint64> seed;
    std::optional<QString> bindAddress;
    std::optional<QString> gameMode;
    std::optional<QString> serverName;
    std::optional<QString> autotestLog;
    std::optional<QString> configFile;
    std::optional<QString> logLevel;
    std::optional<QString> logFile;
    std::optional<QString> logFormat;
    std::optional<QString> assetRoot;
    std::optional<QString> assetManifest;
    bool assetReport = false;
};

struct ServerCommandLineResult
{
    bool success = false;
    ServerCommandLineOptions options;
    QString error;
};

ServerCommandLineResult parseServerCommandLine(const QStringList &arguments);
QString serverCommandLineHelpText();

#endif

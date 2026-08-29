#include "server-command-line.h"
#include "server-config.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextStream>

#include <functional>
#include <limits>

namespace
{
bool expect(bool condition, const char *message)
{
    if (condition)
        return true;
    qCritical().noquote() << message;
    return false;
}

bool parsesDefaults()
{
    const ServerCommandLineResult result = parseServerCommandLine({QStringLiteral("server")});
    return expect(result.success, "default CLI parse failed")
        && expect(!result.options.port.has_value(), "default port must be inherited")
        && expect(!result.options.bindAddress.has_value(), "default bind address must be inherited")
        && expect(!result.options.seed.has_value(), "default seed must remain random");
}

bool parsesCompleteOverrideSet()
{
    const ServerCommandLineResult result = parseServerCommandLine({
        QStringLiteral("server"),
        QStringLiteral("--port"), QStringLiteral("19527"),
        QStringLiteral("--bind-address"), QStringLiteral("127.0.0.1"),
        QStringLiteral("--game-mode"), QStringLiteral("02p"),
        QStringLiteral("--server-name"), QStringLiteral("CLI server"),
        QStringLiteral("--operation-timeout"), QStringLiteral("0"),
        QStringLiteral("--ai"), QStringLiteral("off"),
        QStringLiteral("--ai-delay"), QStringLiteral("250"),
        QStringLiteral("--seed"), QStringLiteral("18446744073709551615"),
        QStringLiteral("--autotest-log"), QStringLiteral("/tmp/server.log"),
        QStringLiteral("--log-level"), QStringLiteral("warning"),
        QStringLiteral("--log-file"), QStringLiteral("/tmp/production.log"),
        QStringLiteral("--log-format"), QStringLiteral("json"),
        QStringLiteral("--config"), QStringLiteral("/tmp/server.ini"),
        QStringLiteral("--check-config"),
        QStringLiteral("--print-config"),
        QStringLiteral("--json")
    });
    return expect(result.success, "complete CLI override parse failed")
        && expect(result.options.port == 19527, "port override mismatch")
        && expect(result.options.bindAddress == QStringLiteral("127.0.0.1"),
                  "bind-address override mismatch")
        && expect(result.options.gameMode == QStringLiteral("02p"), "game mode mismatch")
        && expect(result.options.serverName == QStringLiteral("CLI server"), "server name mismatch")
        && expect(result.options.operationTimeout == 0, "timeout override mismatch")
        && expect(result.options.aiEnabled == false, "AI override mismatch")
        && expect(result.options.aiDelay == 250, "AI delay mismatch")
        && expect(result.options.seed == std::numeric_limits<quint64>::max(), "seed mismatch")
        && expect(result.options.autotestLog == QStringLiteral("/tmp/server.log"),
                  "autotest log mismatch")
        && expect(result.options.logLevel == QStringLiteral("warning"),
                  "log level mismatch")
        && expect(result.options.logFile == QStringLiteral("/tmp/production.log"),
                  "log file mismatch")
        && expect(result.options.logFormat == QStringLiteral("json"),
                  "log format mismatch")
        && expect(result.options.configFile == QStringLiteral("/tmp/server.ini"),
                  "config path mismatch")
        && expect(result.options.checkConfig, "check-config flag missing")
        && expect(result.options.printConfig, "print-config flag missing")
        && expect(result.options.jsonOutput, "JSON flag missing");
}

bool parsesShortOptions()
{
    const ServerCommandLineResult result = parseServerCommandLine({
        QStringLiteral("server"),
        QStringLiteral("-p=9527"),
        QStringLiteral("-m=02p"),
        QStringLiteral("-n=short-options"),
        QStringLiteral("-s=42")
    });
    return expect(result.success, "short CLI option parse failed")
        && expect(result.options.port == 9527, "short port override mismatch")
        && expect(result.options.gameMode == QStringLiteral("02p"), "short game mode mismatch")
        && expect(result.options.serverName == QStringLiteral("short-options"),
                  "short server name mismatch")
        && expect(result.options.seed == 42, "short seed mismatch");
}

bool parsesEphemeralPort()
{
    const ServerCommandLineResult result = parseServerCommandLine({
        QStringLiteral("server"), QStringLiteral("--port"), QStringLiteral("0")
    });
    return expect(result.success, "ephemeral port CLI parse failed")
        && expect(result.options.port.has_value(), "ephemeral port override missing")
        && expect(*result.options.port == 0, "ephemeral port override mismatch");
}

bool rejectsInvalidArguments()
{
    struct InvalidCase
    {
        QStringList arguments;
        QString errorFragment;
    };
    const QList<InvalidCase> cases = {
        {{QStringLiteral("server"), QStringLiteral("--port"), QStringLiteral("-1")},
         QStringLiteral("invalid --port")},
        {{QStringLiteral("server"), QStringLiteral("--port"), QStringLiteral("65536")},
         QStringLiteral("invalid --port")},
        {{QStringLiteral("server"), QStringLiteral("--bind-address"), QStringLiteral("localhost")},
         QStringLiteral("invalid --bind-address")},
        {{QStringLiteral("server"), QStringLiteral("--operation-timeout"), QStringLiteral("-1")},
         QStringLiteral("invalid --operation-timeout")},
        {{QStringLiteral("server"), QStringLiteral("--ai"), QStringLiteral("maybe")},
         QStringLiteral("invalid --ai")},
        {{QStringLiteral("server"), QStringLiteral("--log-level"), QStringLiteral("verbose")},
         QStringLiteral("invalid --log-level")},
        {{QStringLiteral("server"), QStringLiteral("--log-format"), QStringLiteral("xml")},
         QStringLiteral("invalid --log-format")},
        {{QStringLiteral("server"), QStringLiteral("--seed"), QStringLiteral("-1")},
         QStringLiteral("invalid --seed")},
        {{QStringLiteral("server"), QStringLiteral("--port"), QStringLiteral("9527"),
          QStringLiteral("--port"), QStringLiteral("9528")},
         QStringLiteral("may only be specified once")},
        {{QStringLiteral("server"), QStringLiteral("unexpected")},
         QStringLiteral("unexpected positional argument")},
        {{QStringLiteral("server"), QStringLiteral("--unknown")},
         QStringLiteral("Unknown option")},
        {{QStringLiteral("server"), QStringLiteral("--json")},
         QStringLiteral("requires '--print-config'")},
        {{QStringLiteral("server"), QStringLiteral("--print-config"), QStringLiteral("--json"),
          QStringLiteral("--list-game-modes")},
         QStringLiteral("cannot be combined")},
        {{QStringLiteral("server"), QStringLiteral("--config"), QStringLiteral("one.ini"),
          QStringLiteral("--config"), QStringLiteral("two.ini")},
         QStringLiteral("may only be specified once")}
    };

    for (const InvalidCase &test : cases) {
        const ServerCommandLineResult result = parseServerCommandLine(test.arguments);
        if (!expect(!result.success, "invalid CLI arguments were accepted"))
            return false;
        if (!expect(result.error.contains(test.errorFragment, Qt::CaseInsensitive),
                    "CLI error did not contain the expected diagnostic")) {
            qCritical().noquote() << "actual error:" << result.error;
            return false;
        }
    }
    return true;
}

bool exposesDiscoverableHelp()
{
    const QString help = serverCommandLineHelpText();
    return expect(help.contains(QStringLiteral("--bind-address")), "help omits bind address")
        && expect(help.contains(QStringLiteral("--list-game-modes")), "help omits game mode listing")
        && expect(help.contains(QStringLiteral("--config")), "help omits config file")
        && expect(help.contains(QStringLiteral("--check-config")), "help omits config validation")
        && expect(help.contains(QStringLiteral("--print-config")), "help omits config output")
        && expect(help.contains(QStringLiteral("--json")), "help omits JSON output")
        && expect(help.contains(QStringLiteral("--log-level")), "help omits log level")
        && expect(help.contains(QStringLiteral("--log-file")), "help omits log file")
        && expect(help.contains(QStringLiteral("--log-format")), "help omits log format")
        && expect(help.contains(QStringLiteral("--operation-timeout")), "help omits timeout");
}

bool loadsAndValidatesConfigFiles()
{
    QTemporaryDir directory;
    if (!expect(directory.isValid(), "temporary config directory creation failed"))
        return false;

    const QString validPath = directory.filePath(QStringLiteral("server.ini"));
    QFile validFile(validPath);
    if (!expect(validFile.open(QIODevice::WriteOnly | QIODevice::Text),
                "valid config file creation failed"))
        return false;
    QTextStream(&validFile)
        << "[General]\n"
        << "ServerName=Unit Test Server\n"
        << "GameMode=10p\n"
        << "ServerPort=9527\n"
        << "BindAddress=127.0.0.1\n"
        << "DisableChat=true\n"
        << "BanPackages=nostalgia, test\n"
        << "BossModeDifficulty=63\n"
        << "[3v3]\n"
        << "RoleChoose=AllRoles\n"
        << "[Banlist]\n"
        << "Roles=caocao, simayi\n";
    validFile.close();

    const ServerConfigLoadResult valid = loadServerConfigFile(validPath);
    if (!expect(valid.success, "valid server config was rejected")) {
        qCritical().noquote() << valid.errors.join(QLatin1Char('\n'));
        return false;
    }
    if (!expect(valid.values.value(QStringLiteral("ServerPort")).toLongLong() == 9527,
                "config port mismatch")
        || !expect(valid.values.value(QStringLiteral("DisableChat")).toBool(),
                   "config boolean mismatch")
        || !expect(valid.values.value(QStringLiteral("BanPackages")).toStringList()
                       == QStringList({QStringLiteral("nostalgia"), QStringLiteral("test")}),
                   "config list mismatch")
        || !expect(validateServerConfigValues(valid.values).isEmpty(),
                   "normalized config failed validation")
        || !expect(serverConfigJson(valid.values).contains(QStringLiteral("\"GameMode\"")),
                   "JSON config output omitted GameMode")
        || !expect(serverConfigText(valid.values).contains(QStringLiteral("ServerPort=9527")),
                   "text config output omitted ServerPort"))
        return false;

    const QString invalidPath = directory.filePath(QStringLiteral("invalid.ini"));
    QFile invalidFile(invalidPath);
    if (!expect(invalidFile.open(QIODevice::WriteOnly | QIODevice::Text),
                "invalid config file creation failed"))
        return false;
    QTextStream(&invalidFile)
        << "[General]\n"
        << "ServerPort=70000\n"
        << "DisableChat=perhaps\n"
        << "TypoSetting=true\n";
    invalidFile.close();

    const ServerConfigLoadResult invalid = loadServerConfigFile(invalidPath);
    return expect(!invalid.success, "invalid server config was accepted")
        && expect(invalid.errors.size() == 3, "invalid config did not report every error")
        && expect(!loadServerConfigFile(directory.filePath(QStringLiteral("missing.ini"))).success,
                   "missing config file was accepted");
}

bool runsParserContract()
{
    bool passed = true;
    if (!parsesDefaults())
        passed = false;
    if (!parsesCompleteOverrideSet())
        passed = false;
    if (!parsesShortOptions())
        passed = false;
    if (!parsesEphemeralPort())
        passed = false;
    if (!rejectsInvalidArguments())
        passed = false;
    if (!exposesDiscoverableHelp())
        passed = false;
    if (!loadsAndValidatesConfigFiles())
        passed = false;
    return passed;
}

struct ProcessResult
{
    bool started = false;
    bool timedOut = false;
    QProcess::ExitStatus exitStatus = QProcess::NormalExit;
    int exitCode = -1;
    QByteArray output;
    QString error;
};

ProcessResult runServerProcess(const QString &serverPath, const QStringList &arguments)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(serverPath, arguments);

    ProcessResult result;
    result.started = process.waitForStarted(30000);
    if (!result.started) {
        result.error = process.errorString();
        return result;
    }
    if (!process.waitForFinished(30000)) {
        result.timedOut = true;
        process.terminate();
        if (!process.waitForFinished(5000)) {
            process.kill();
            process.waitForFinished(5000);
        }
    }
    result.exitStatus = process.exitStatus();
    result.exitCode = process.exitCode();
    result.output = process.readAll();
    result.error = process.errorString();
    return result;
}

bool processSucceeded(const ProcessResult &result, const char *caseName)
{
    if (result.started && !result.timedOut
        && result.exitStatus == QProcess::NormalExit && result.exitCode == 0) {
        return true;
    }
    qCritical().noquote() << caseName << "failed:"
                          << (result.started ? QStringLiteral("started")
                                             : QStringLiteral("start failed"))
                          << (result.timedOut ? QStringLiteral("timeout") : QString())
                          << "exit" << result.exitCode << result.error;
    if (!result.output.isEmpty())
        qCritical().noquote() << QString::fromUtf8(result.output);
    return false;
}

bool validatesHelpProcess(const QString &serverPath)
{
    const ProcessResult result = runServerProcess(serverPath, {QStringLiteral("--help")});
    return processSucceeded(result, "--help")
        && expect(result.output.contains("--bind-address"), "server --help omits --bind-address");
}

bool validatesVersionProcess(const QString &serverPath)
{
    const ProcessResult result = runServerProcess(serverPath, {QStringLiteral("--version")});
    const QRegularExpression versionPattern(QStringLiteral("qsanguosha_server [0-9]+"));
    return processSucceeded(result, "--version")
        && expect(versionPattern.match(QString::fromUtf8(result.output)).hasMatch(),
            "server --version output is invalid");
}

bool validatesConfigProcess(const QString &serverPath, const QString &configPath)
{
    const ProcessResult result = runServerProcess(serverPath, {
        QStringLiteral("--config"), configPath, QStringLiteral("--check-config")});
    return processSucceeded(result, "config validation")
        && expect(result.output.contains("Configuration OK"),
            "server config validation omitted success marker");
}

bool validatesConfigPrecedenceProcess(const QString &serverPath, const QString &configPath)
{
    const ProcessResult result = runServerProcess(serverPath, {
        QStringLiteral("--config"), configPath,
        QStringLiteral("--port"), QStringLiteral("19527"),
        QStringLiteral("--print-config"), QStringLiteral("--json")});
    if (!processSucceeded(result, "config precedence"))
        return false;

    const int objectStart = result.output.indexOf('{');
    const int objectEnd = result.output.lastIndexOf('}');
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(
        objectStart >= 0 && objectEnd >= objectStart
            ? result.output.mid(objectStart, objectEnd - objectStart + 1)
            : QByteArray(),
        &error);
    if (!expect(error.error == QJsonParseError::NoError && document.isObject(),
            "server config precedence output is not JSON")) {
        qCritical().noquote() << QString::fromUtf8(result.output);
        return false;
    }
    const QJsonObject config = document.object();
    return expect(config.value(QStringLiteral("DisableChat")).toBool(),
               "config file boolean was not loaded")
        && expect(config.value(QStringLiteral("GameMode")).toString() == QLatin1String("10p"),
            "config file game mode was not loaded")
        && expect(config.value(QStringLiteral("ServerPort")).toInt() == 19527,
            "CLI port did not override config file");
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QString serverPath;
    QString configPath;
    const QStringList arguments = application.arguments();
    for (int i = 1; i < arguments.size(); ++i) {
        if (arguments.at(i) == QLatin1String("--server") && i + 1 < arguments.size()) {
            serverPath = arguments.at(++i);
        } else if (arguments.at(i) == QLatin1String("--config") && i + 1 < arguments.size()) {
            configPath = arguments.at(++i);
        } else {
            qCritical().noquote() << "Unknown or incomplete argument:" << arguments.at(i);
            return 64;
        }
    }

    struct NamedCase
    {
        QString name;
        std::function<bool()> run;
    };
    QList<NamedCase> cases = {
        {QStringLiteral("parser"), []() { return runsParserContract(); }}
    };
    if (!serverPath.isEmpty() || !configPath.isEmpty()) {
        if (serverPath.isEmpty() || configPath.isEmpty()) {
            qCritical().noquote() << "--server and --config must be provided together";
            return 64;
        }
        cases.append({QStringLiteral("--help"),
            [serverPath]() { return validatesHelpProcess(serverPath); }});
        cases.append({QStringLiteral("--version"),
            [serverPath]() { return validatesVersionProcess(serverPath); }});
        cases.append({QStringLiteral("config validation"),
            [serverPath, configPath]() { return validatesConfigProcess(serverPath, configPath); }});
        cases.append({QStringLiteral("config/CLI precedence"),
            [serverPath, configPath]() {
                return validatesConfigPrecedenceProcess(serverPath, configPath);
            }});
    }

    QList<bool> results;
    int passedCount = 0;
    for (const NamedCase &testCase : cases) {
        const bool passed = testCase.run();
        results.append(passed);
        passedCount += passed ? 1 : 0;
        qInfo().noquote() << (passed ? QStringLiteral("[PASS]") : QStringLiteral("[FAIL]"))
                          << testCase.name;
    }

    qInfo().noquote() << "\nSERVER_CLI_RESULT\n";
    for (int i = 0; i < cases.size(); ++i)
        qInfo().noquote() << (results.at(i) ? QStringLiteral("PASS") : QStringLiteral("FAIL"))
                          << cases.at(i).name;
    qInfo().noquote() << QStringLiteral("\nTOTAL: %1\nPASS: %2\nFAIL: %3")
                             .arg(cases.size()).arg(passedCount).arg(cases.size() - passedCount);
    return passedCount == cases.size() ? 0 : 1;
}

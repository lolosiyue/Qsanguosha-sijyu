#include "server-command-line.h"
#include "server-config.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

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
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (!parsesDefaults())
        return 1;
    if (!parsesCompleteOverrideSet())
        return 2;
    if (!parsesShortOptions())
        return 3;
    if (!parsesEphemeralPort())
        return 4;
    if (!rejectsInvalidArguments())
        return 5;
    if (!exposesDiscoverableHelp())
        return 6;
    if (!loadsAndValidatesConfigFiles())
        return 7;
    return 0;
}

#include "server-command-line.h"

#include <QCoreApplication>
#include <QDebug>

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
        QStringLiteral("--print-config")
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
        && expect(result.options.printConfig, "print-config flag missing");
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

bool rejectsInvalidArguments()
{
    struct InvalidCase
    {
        QStringList arguments;
        QString errorFragment;
    };
    const QList<InvalidCase> cases = {
        {{QStringLiteral("server"), QStringLiteral("--port"), QStringLiteral("0")},
         QStringLiteral("invalid --port")},
        {{QStringLiteral("server"), QStringLiteral("--port"), QStringLiteral("65536")},
         QStringLiteral("invalid --port")},
        {{QStringLiteral("server"), QStringLiteral("--bind-address"), QStringLiteral("localhost")},
         QStringLiteral("invalid --bind-address")},
        {{QStringLiteral("server"), QStringLiteral("--operation-timeout"), QStringLiteral("-1")},
         QStringLiteral("invalid --operation-timeout")},
        {{QStringLiteral("server"), QStringLiteral("--ai"), QStringLiteral("maybe")},
         QStringLiteral("invalid --ai")},
        {{QStringLiteral("server"), QStringLiteral("--seed"), QStringLiteral("-1")},
         QStringLiteral("invalid --seed")},
        {{QStringLiteral("server"), QStringLiteral("--port"), QStringLiteral("9527"),
          QStringLiteral("--port"), QStringLiteral("9528")},
         QStringLiteral("may only be specified once")},
        {{QStringLiteral("server"), QStringLiteral("unexpected")},
         QStringLiteral("unexpected positional argument")},
        {{QStringLiteral("server"), QStringLiteral("--unknown")},
         QStringLiteral("Unknown option")}
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
        && expect(help.contains(QStringLiteral("--print-config")), "help omits config output")
        && expect(help.contains(QStringLiteral("--operation-timeout")), "help omits timeout");
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
    if (!rejectsInvalidArguments())
        return 4;
    if (!exposesDiscoverableHelp())
        return 5;
    return 0;
}

#include "server-command-line.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QHostAddress>

namespace
{
void configureParser(QCommandLineParser &parser)
{
    parser.setApplicationDescription(QStringLiteral("QSanguosha dedicated headless server"));
    parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
    parser.addOption(QCommandLineOption({QStringLiteral("h"), QStringLiteral("help")},
        QStringLiteral("Show this help and exit.")));
    parser.addOption(QCommandLineOption({QStringLiteral("v"), QStringLiteral("version")},
        QStringLiteral("Show the server version and exit.")));
    parser.addOption(QCommandLineOption({QStringLiteral("p"), QStringLiteral("port")},
        QStringLiteral("Listen on TCP port <port> (1-65535)."), QStringLiteral("port")));
    parser.addOption(QCommandLineOption(QStringLiteral("bind-address"),
        QStringLiteral("Bind to an IP address, any, any-ipv4, or any-ipv6."),
        QStringLiteral("address")));
    parser.addOption(QCommandLineOption({QStringLiteral("m"), QStringLiteral("game-mode")},
        QStringLiteral("Use game mode <id> for this run."), QStringLiteral("id")));
    parser.addOption(QCommandLineOption({QStringLiteral("n"), QStringLiteral("server-name")},
        QStringLiteral("Advertise <name> as the server name."), QStringLiteral("name")));
    parser.addOption(QCommandLineOption(QStringLiteral("operation-timeout"),
        QStringLiteral("Set the operation timeout in seconds; 0 disables it."),
        QStringLiteral("seconds")));
    parser.addOption(QCommandLineOption(QStringLiteral("ai"),
        QStringLiteral("Enable or disable server AI (on|off)."), QStringLiteral("state")));
    parser.addOption(QCommandLineOption(QStringLiteral("ai-delay"),
        QStringLiteral("Set the AI delay in milliseconds (0-600000)."),
        QStringLiteral("milliseconds")));
    parser.addOption(QCommandLineOption({QStringLiteral("s"), QStringLiteral("seed")},
        QStringLiteral("Use an unsigned 64-bit deterministic game seed."),
        QStringLiteral("seed")));
    parser.addOption(QCommandLineOption(QStringLiteral("autotest-log"),
        QStringLiteral("Write automation markers to <path>."), QStringLiteral("path")));
    parser.addOption(QCommandLineOption(QStringLiteral("list-game-modes"),
        QStringLiteral("List available game modes and exit.")));
    parser.addOption(QCommandLineOption(QStringLiteral("print-config"),
        QStringLiteral("Print the effective server configuration and exit.")));
}

bool rejectDuplicateValue(const QCommandLineParser &parser, const QString &name, QString &error)
{
    if (parser.values(name).size() <= 1)
        return true;
    error = QStringLiteral("option '--%1' may only be specified once").arg(name);
    return false;
}

bool parseBoundedInteger(const QCommandLineParser &parser, const QString &name,
                         qlonglong minimum, qlonglong maximum, qlonglong &result,
                         QString &error)
{
    if (!rejectDuplicateValue(parser, name, error))
        return false;
    bool ok = false;
    result = parser.value(name).toLongLong(&ok, 10);
    if (!ok || result < minimum || result > maximum) {
        error = QStringLiteral("invalid --%1 value '%2' (expected %3-%4)")
            .arg(name, parser.value(name))
            .arg(minimum)
            .arg(maximum);
        return false;
    }
    return true;
}

bool parseSeed(const QCommandLineParser &parser, quint64 &seed, QString &error)
{
    const QString name = QStringLiteral("seed");
    if (!rejectDuplicateValue(parser, name, error))
        return false;
    const QString value = parser.value(name);
    if (value.isEmpty()) {
        error = QStringLiteral("invalid --seed value: an unsigned integer is required");
        return false;
    }
    for (const QChar character : value) {
        if (character < QLatin1Char('0') || character > QLatin1Char('9')) {
            error = QStringLiteral("invalid --seed value '%1' (expected an unsigned 64-bit integer)")
                .arg(value);
            return false;
        }
    }
    bool ok = false;
    seed = value.toULongLong(&ok, 10);
    if (!ok) {
        error = QStringLiteral("invalid --seed value '%1' (expected an unsigned 64-bit integer)")
            .arg(value);
        return false;
    }
    return true;
}

bool parseBindAddress(const QCommandLineParser &parser, QString &address, QString &error)
{
    const QString name = QStringLiteral("bind-address");
    if (!rejectDuplicateValue(parser, name, error))
        return false;
    const QString value = parser.value(name).trimmed();
    const QString normalized = value.toLower();
    if (normalized == QLatin1String("any")
        || normalized == QLatin1String("any-ipv4")
        || normalized == QLatin1String("any-ipv6")) {
        address = normalized;
        return true;
    }
    QHostAddress parsed;
    if (value.isEmpty() || !parsed.setAddress(value)) {
        error = QStringLiteral("invalid --bind-address value '%1' (expected an IP address)")
            .arg(value);
        return false;
    }
    address = parsed.toString();
    return true;
}
}

ServerCommandLineResult parseServerCommandLine(const QStringList &arguments)
{
    ServerCommandLineResult result;
    if (arguments.isEmpty()) {
        result.error = QStringLiteral("missing program name");
        return result;
    }

    QCommandLineParser parser;
    configureParser(parser);
    if (!parser.parse(arguments)) {
        result.error = parser.errorText();
        return result;
    }
    if (!parser.positionalArguments().isEmpty()) {
        result.error = QStringLiteral("unexpected positional argument '%1'")
            .arg(parser.positionalArguments().constFirst());
        return result;
    }

    result.options.helpRequested = parser.isSet(QStringLiteral("help"));
    result.options.versionRequested = parser.isSet(QStringLiteral("version"));
    result.options.listGameModes = parser.isSet(QStringLiteral("list-game-modes"));
    result.options.printConfig = parser.isSet(QStringLiteral("print-config"));

    qlonglong integer = 0;
    if (parser.isSet(QStringLiteral("port"))) {
        if (!parseBoundedInteger(parser, QStringLiteral("port"), 1, 65535,
                                 integer, result.error))
            return result;
        result.options.port = static_cast<quint16>(integer);
    }
    if (parser.isSet(QStringLiteral("operation-timeout"))) {
        if (!parseBoundedInteger(parser, QStringLiteral("operation-timeout"), 0, 86400,
                                 integer, result.error))
            return result;
        result.options.operationTimeout = static_cast<int>(integer);
    }
    if (parser.isSet(QStringLiteral("ai-delay"))) {
        if (!parseBoundedInteger(parser, QStringLiteral("ai-delay"), 0, 600000,
                                 integer, result.error))
            return result;
        result.options.aiDelay = static_cast<int>(integer);
    }
    if (parser.isSet(QStringLiteral("seed"))) {
        quint64 seed = 0;
        if (!parseSeed(parser, seed, result.error))
            return result;
        result.options.seed = seed;
    }
    if (parser.isSet(QStringLiteral("bind-address"))) {
        QString address;
        if (!parseBindAddress(parser, address, result.error))
            return result;
        result.options.bindAddress = address;
    }

    const auto parseNonEmptyText = [&parser, &result](const QString &name,
                                                       std::optional<QString> &target) {
        if (!parser.isSet(name))
            return true;
        if (!rejectDuplicateValue(parser, name, result.error))
            return false;
        const QString value = parser.value(name).trimmed();
        if (value.isEmpty()) {
            result.error = QStringLiteral("option '--%1' requires a non-empty value").arg(name);
            return false;
        }
        target = value;
        return true;
    };
    if (!parseNonEmptyText(QStringLiteral("game-mode"), result.options.gameMode)
        || !parseNonEmptyText(QStringLiteral("server-name"), result.options.serverName)
        || !parseNonEmptyText(QStringLiteral("autotest-log"), result.options.autotestLog))
        return result;

    if (parser.isSet(QStringLiteral("ai"))) {
        if (!rejectDuplicateValue(parser, QStringLiteral("ai"), result.error))
            return result;
        const QString value = parser.value(QStringLiteral("ai")).trimmed().toLower();
        if (value == QLatin1String("on") || value == QLatin1String("true")
            || value == QLatin1String("1")) {
            result.options.aiEnabled = true;
        } else if (value == QLatin1String("off") || value == QLatin1String("false")
                   || value == QLatin1String("0")) {
            result.options.aiEnabled = false;
        } else {
            result.error = QStringLiteral("invalid --ai value '%1' (expected on or off)")
                .arg(parser.value(QStringLiteral("ai")));
            return result;
        }
    }

    result.success = true;
    return result;
}

QString serverCommandLineHelpText()
{
    QCommandLineParser parser;
    configureParser(parser);
    return parser.helpText();
}

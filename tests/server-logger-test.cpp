#include "server-logger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>

namespace
{
bool expect(bool condition, const char *message)
{
    if (condition)
        return true;
    qCritical().noquote() << message;
    return false;
}

QList<QJsonObject> readJsonLines(const QString &path)
{
    QFile file(path);
    QList<QJsonObject> records;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return records;
    for (const QByteArray &line : file.readAll().split('\n')) {
        if (line.trimmed().isEmpty())
            continue;
        const QJsonDocument document = QJsonDocument::fromJson(line);
        if (document.isObject())
            records.append(document.object());
    }
    return records;
}

bool validatesParsers()
{
    ServerLogLevel level = ServerLogLevel::Info;
    ServerLogFormat format = ServerLogFormat::Text;
    return expect(parseServerLogLevel(QStringLiteral("DEBUG"), level)
                      && level == ServerLogLevel::Debug,
                  "debug log level was rejected")
        && expect(parseServerLogLevel(QStringLiteral("warning"), level)
                      && level == ServerLogLevel::Warning,
                  "warning log level was rejected")
        && expect(!parseServerLogLevel(QStringLiteral("verbose"), level),
                  "unknown log level was accepted")
        && expect(parseServerLogFormat(QStringLiteral("JSON"), format)
                      && format == ServerLogFormat::Json,
                  "JSON log format was rejected")
        && expect(!parseServerLogFormat(QStringLiteral("xml"), format),
                  "unknown log format was accepted");
}

bool writesStructuredJson(const QTemporaryDir &directory)
{
    const QString path = directory.filePath(QStringLiteral("server.jsonl"));
    QString error;
    {
        ServerLogger logger;
        ServerLogConfiguration configuration;
        configuration.level = ServerLogLevel::Info;
        configuration.format = ServerLogFormat::Json;
        configuration.filePath = path;
        if (!expect(logger.start(configuration, error), "JSON logger failed to start"))
            return false;
        logger.debug(QStringLiteral("server"), QStringLiteral("filtered"));
        logger.info(QStringLiteral("player"), QStringLiteral("joined"), 3,
                    QStringLiteral("p001"),
                    {{QStringLiteral("name"), QStringLiteral("Alice")}});
        qWarning().noquote() << "captured Qt warning";
    }

    const QList<QJsonObject> records = readJsonLines(path);
    if (!expect(records.size() == 2, "JSON logger filtering or line count mismatch"))
        return false;
    const QJsonObject joined = records.at(0);
    const QJsonObject warning = records.at(1);
    const QDateTime timestamp = QDateTime::fromString(
        joined.value(QStringLiteral("timestamp")).toString(), Qt::ISODateWithMs);
    return expect(timestamp.isValid(), "JSON timestamp is invalid")
        && expect(joined.value(QStringLiteral("level")).toString() == QLatin1String("info"),
                  "JSON level mismatch")
        && expect(joined.value(QStringLiteral("component")).toString()
                      == QLatin1String("player"),
                  "JSON component mismatch")
        && expect(joined.value(QStringLiteral("room_id")).toInt() == 3,
                  "JSON room_id mismatch")
        && expect(joined.value(QStringLiteral("player_id")).toString()
                      == QLatin1String("p001"),
                  "JSON player_id mismatch")
        && expect(joined.value(QStringLiteral("message")).toString()
                      == QLatin1String("joined"),
                  "JSON message mismatch")
        && expect(joined.value(QStringLiteral("name")).toString()
                      == QLatin1String("Alice"),
                  "JSON custom field mismatch")
        && expect(warning.value(QStringLiteral("level")).toString()
                      == QLatin1String("warning"),
                  "Qt warning level mismatch")
        && expect(warning.value(QStringLiteral("component")).toString()
                      == QLatin1String("qt"),
                  "Qt warning component mismatch")
        && expect(warning.value(QStringLiteral("room_id")).isNull(),
                  "missing JSON room_id must be null")
        && expect(warning.value(QStringLiteral("player_id")).isNull(),
                  "missing JSON player_id must be null");
}

bool writesTextAndRejectsBadPath(const QTemporaryDir &directory)
{
    const QString path = directory.filePath(QStringLiteral("server.log"));
    QString error;
    {
        ServerLogger logger;
        ServerLogConfiguration configuration;
        configuration.level = ServerLogLevel::Warning;
        configuration.filePath = path;
        if (!expect(logger.start(configuration, error), "text logger failed to start"))
            return false;
        logger.info(QStringLiteral("server"), QStringLiteral("filtered"));
        logger.warning(QStringLiteral("player"), QStringLiteral("disconnected"), 7,
                       QStringLiteral("p009"),
                       {{QStringLiteral("reason"), QStringLiteral("timeout")}});
    }

    QFile file(path);
    if (!expect(file.open(QIODevice::ReadOnly | QIODevice::Text),
                "text log file could not be read"))
        return false;
    const QString text = QString::fromUtf8(file.readAll());
    const QRegularExpression pattern(QStringLiteral(
        "^\\d{4}-\\d{2}-\\d{2}T.*Z WARNING player room_id=7 "
        "player_id=p009 disconnected reason=timeout\\n$"));
    if (!expect(pattern.match(text).hasMatch(), "text log record mismatch"))
        return false;

    ServerLogger invalid;
    ServerLogConfiguration invalidConfiguration;
    invalidConfiguration.filePath = directory.filePath(
        QStringLiteral("missing/server.log"));
    return expect(!invalid.start(invalidConfiguration, error),
                  "logger accepted a file in a missing directory")
        && expect(error.contains(QStringLiteral("unable to open log file")),
                  "log file failure omitted its diagnostic");
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    if (!expect(directory.isValid(), "temporary log directory creation failed"))
        return 1;
    if (!validatesParsers())
        return 2;
    if (!writesStructuredJson(directory))
        return 3;
    if (!writesTextAndRejectsBadPath(directory))
        return 4;
    return 0;
}

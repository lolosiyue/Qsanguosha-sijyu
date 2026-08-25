#include "server-logger.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMutexLocker>
#include <QRegularExpression>

#include <cstdio>

std::atomic<ServerLogger *> ServerLogger::s_activeLogger { nullptr };

namespace
{
QString normalizedComponent(const QString &component)
{
    const QString result = component.trimmed();
    return result.isEmpty() ? QStringLiteral("server") : result;
}

QString normalizedMessage(const QString &message)
{
    QString result = message;
    while (result.endsWith(QLatin1Char('\n')) || result.endsWith(QLatin1Char('\r')))
        result.chop(1);
    return result;
}

QString textValue(const QVariant &value)
{
    if (!value.isValid() || value.isNull())
        return QStringLiteral("null");
    if (value.metaType().id() == QMetaType::Bool)
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");

    const QString text = value.toString();
    static const QRegularExpression unquoted(
        QStringLiteral("^[A-Za-z0-9_./:@+-]+$"));
    if (unquoted.match(text).hasMatch())
        return text;

    QByteArray encoded = QJsonDocument(QJsonArray { text }).toJson(QJsonDocument::Compact);
    if (encoded.size() >= 2) {
        encoded.remove(0, 1);
        encoded.chop(1);
    }
    return QString::fromUtf8(encoded);
}

ServerLogLevel qtLogLevel(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return ServerLogLevel::Debug;
    case QtInfoMsg:
        return ServerLogLevel::Info;
    case QtWarningMsg:
        return ServerLogLevel::Warning;
    case QtCriticalMsg:
    case QtFatalMsg:
        return ServerLogLevel::Error;
    }
    return ServerLogLevel::Info;
}
}

bool parseServerLogLevel(const QString &value, ServerLogLevel &level)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QLatin1String("debug"))
        level = ServerLogLevel::Debug;
    else if (normalized == QLatin1String("info"))
        level = ServerLogLevel::Info;
    else if (normalized == QLatin1String("warning"))
        level = ServerLogLevel::Warning;
    else if (normalized == QLatin1String("error"))
        level = ServerLogLevel::Error;
    else
        return false;
    return true;
}

bool parseServerLogFormat(const QString &value, ServerLogFormat &format)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QLatin1String("text"))
        format = ServerLogFormat::Text;
    else if (normalized == QLatin1String("json"))
        format = ServerLogFormat::Json;
    else
        return false;
    return true;
}

QString serverLogLevelName(ServerLogLevel level)
{
    switch (level) {
    case ServerLogLevel::Debug:
        return QStringLiteral("debug");
    case ServerLogLevel::Info:
        return QStringLiteral("info");
    case ServerLogLevel::Warning:
        return QStringLiteral("warning");
    case ServerLogLevel::Error:
        return QStringLiteral("error");
    }
    return QStringLiteral("info");
}

ServerLogger::~ServerLogger()
{
    stop();
}

bool ServerLogger::start(const ServerLogConfiguration &configuration, QString &error)
{
    if (m_started) {
        error = QStringLiteral("server logger is already running");
        return false;
    }

    if (!configuration.filePath.isEmpty()) {
        m_file.setFileName(configuration.filePath);
        if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            error = QStringLiteral("unable to open log file '%1': %2")
                .arg(configuration.filePath, m_file.errorString());
            return false;
        }
    }

    ServerLogger *expected = nullptr;
    if (!s_activeLogger.compare_exchange_strong(expected, this)) {
        if (m_file.isOpen())
            m_file.close();
        error = QStringLiteral("another server logger is already active");
        return false;
    }

    m_configuration = configuration;
    m_previousHandler = qInstallMessageHandler(&ServerLogger::qtMessageHandler);
    m_started = true;
    return true;
}

void ServerLogger::stop()
{
    if (!m_started)
        return;

    qInstallMessageHandler(m_previousHandler);
    ServerLogger *expected = this;
    s_activeLogger.compare_exchange_strong(expected, nullptr);

    QMutexLocker locker(&m_mutex);
    if (m_file.isOpen()) {
        m_file.flush();
        m_file.close();
    }
    m_started = false;
}

void ServerLogger::log(ServerLogLevel level, const QString &component,
                       const QString &message, int roomId,
                       const QString &playerId, const QVariantMap &fields)
{
    if (!m_started || static_cast<int>(level) < static_cast<int>(m_configuration.level))
        return;

    const QByteArray record = formatRecord(level, component, message, roomId,
                                           playerId, fields) + '\n';
    QMutexLocker locker(&m_mutex);
    if (m_file.isOpen()) {
        m_file.write(record);
        m_file.flush();
    } else {
        std::fwrite(record.constData(), 1, size_t(record.size()), stdout);
        std::fflush(stdout);
    }
}

void ServerLogger::debug(const QString &component, const QString &message,
                         const QVariantMap &fields)
{
    log(ServerLogLevel::Debug, component, message, -1, QString(), fields);
}

void ServerLogger::info(const QString &component, const QString &message,
                        int roomId, const QString &playerId,
                        const QVariantMap &fields)
{
    log(ServerLogLevel::Info, component, message, roomId, playerId, fields);
}

void ServerLogger::warning(const QString &component, const QString &message,
                           int roomId, const QString &playerId,
                           const QVariantMap &fields)
{
    log(ServerLogLevel::Warning, component, message, roomId, playerId, fields);
}

void ServerLogger::error(const QString &component, const QString &message,
                         int roomId, const QString &playerId,
                         const QVariantMap &fields)
{
    log(ServerLogLevel::Error, component, message, roomId, playerId, fields);
}

void ServerLogger::qtMessageHandler(QtMsgType type,
                                    const QMessageLogContext &context,
                                    const QString &message)
{
    ServerLogger *logger = s_activeLogger.load();
    if (!logger)
        return;

    const QString category = QString::fromUtf8(context.category ? context.category : "");
    logger->log(qtLogLevel(type),
                category.isEmpty() || category == QLatin1String("default")
                    ? QStringLiteral("qt") : category,
                message);
}

QByteArray ServerLogger::formatRecord(ServerLogLevel level,
                                      const QString &component,
                                      const QString &message, int roomId,
                                      const QString &playerId,
                                      const QVariantMap &fields) const
{
    const QString timestamp = QDateTime::currentDateTimeUtc()
        .toString(Qt::ISODateWithMs);
    const QString cleanComponent = normalizedComponent(component);
    const QString cleanMessage = normalizedMessage(message);

    if (m_configuration.format == ServerLogFormat::Json) {
        QJsonObject object;
        object.insert(QStringLiteral("timestamp"), timestamp);
        object.insert(QStringLiteral("level"), serverLogLevelName(level));
        object.insert(QStringLiteral("component"), cleanComponent);
        object.insert(QStringLiteral("room_id"), roomId >= 0
            ? QJsonValue(roomId) : QJsonValue(QJsonValue::Null));
        object.insert(QStringLiteral("player_id"), playerId.isEmpty()
            ? QJsonValue(QJsonValue::Null) : QJsonValue(playerId));
        object.insert(QStringLiteral("message"), cleanMessage);
        for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
            if (!object.contains(it.key()))
                object.insert(it.key(), QJsonValue::fromVariant(it.value()));
        }
        return QJsonDocument(object).toJson(QJsonDocument::Compact);
    }

    QStringList parts;
    parts << timestamp << serverLogLevelName(level).toUpper() << cleanComponent;
    if (roomId >= 0)
        parts << QStringLiteral("room_id=%1").arg(roomId);
    if (!playerId.isEmpty())
        parts << QStringLiteral("player_id=%1").arg(textValue(playerId));
    parts << cleanMessage;
    for (auto it = fields.cbegin(); it != fields.cend(); ++it)
        parts << QStringLiteral("%1=%2").arg(it.key(), textValue(it.value()));
    return parts.join(QLatin1Char(' ')).toUtf8();
}

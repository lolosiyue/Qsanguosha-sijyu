#ifndef QSAN_SERVER_LOGGER_H
#define QSAN_SERVER_LOGGER_H

#include <QFile>
#include <QMutex>
#include <QString>
#include <QVariantMap>
#include <QtLogging>

#include <atomic>

enum class ServerLogLevel
{
    Debug = 0,
    Info,
    Warning,
    Error
};

enum class ServerLogFormat
{
    Text,
    Json
};

struct ServerLogConfiguration
{
    ServerLogLevel level = ServerLogLevel::Info;
    ServerLogFormat format = ServerLogFormat::Text;
    QString filePath;
};

bool parseServerLogLevel(const QString &value, ServerLogLevel &level);
bool parseServerLogFormat(const QString &value, ServerLogFormat &format);
QString serverLogLevelName(ServerLogLevel level);

class ServerLogger final
{
public:
    ServerLogger() = default;
    ~ServerLogger();

    ServerLogger(const ServerLogger &) = delete;
    ServerLogger &operator=(const ServerLogger &) = delete;

    bool start(const ServerLogConfiguration &configuration, QString &error);
    void stop();

    void log(ServerLogLevel level, const QString &component, const QString &message,
             int roomId = -1, const QString &playerId = QString(),
             const QVariantMap &fields = QVariantMap());
    void debug(const QString &component, const QString &message,
               const QVariantMap &fields = QVariantMap());
    void info(const QString &component, const QString &message,
              int roomId = -1, const QString &playerId = QString(),
              const QVariantMap &fields = QVariantMap());
    void warning(const QString &component, const QString &message,
                 int roomId = -1, const QString &playerId = QString(),
                 const QVariantMap &fields = QVariantMap());
    void error(const QString &component, const QString &message,
               int roomId = -1, const QString &playerId = QString(),
               const QVariantMap &fields = QVariantMap());

private:
    static void qtMessageHandler(QtMsgType type, const QMessageLogContext &context,
                                 const QString &message);
    QByteArray formatRecord(ServerLogLevel level, const QString &component,
                            const QString &message, int roomId,
                            const QString &playerId, const QVariantMap &fields) const;

    static std::atomic<ServerLogger *> s_activeLogger;

    ServerLogConfiguration m_configuration;
    QFile m_file;
    QMutex m_mutex;
    QtMessageHandler m_previousHandler = nullptr;
    bool m_started = false;
};

#endif

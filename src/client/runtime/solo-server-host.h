#ifndef QSAN_SOLO_SERVER_HOST_H
#define QSAN_SOLO_SERVER_HOST_H

#include <QJsonObject>
#include <QString>
#include <QByteArray>
#include <QList>
#include <QPointer>

class Server;
class ServerSocket;
class ClientSocket;

class SoloServerHost final
{
public:
    SoloServerHost(const QString &assets, const QString &work, const QString &userData);
    ~SoloServerHost();

    int initialize();
    int start();
    int frame();
    int pump();
    int stop();
    int reportFailure(const QString &message);

private:
    int writeError(const QString &message, int status);
    int writeResult(const QJsonObject &result, int status = 0);
    QString file(const char *name) const;

    QString m_assets, m_work, m_userData;
    QList<QByteArray> m_frames;
    Server *m_server = nullptr;
    QPointer<ServerSocket> m_transport;
    QPointer<ClientSocket> m_client;
    bool m_initialized = false;
    bool m_started = false;
    bool m_preparing = false;
    bool m_stopping = false;
    bool m_closed = false;
    QString m_error;
};

#endif

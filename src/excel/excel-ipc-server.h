#ifndef QSANGUOSHA_EXCEL_IPC_SERVER_H
#define QSANGUOSHA_EXCEL_IPC_SERVER_H

#include <QObject>
#include <QJsonObject>
#include <QUrlQuery>
#include <functional>

class QTcpServer;
class QTcpSocket;

class ExcelIpcServer : public QObject
{
    Q_OBJECT
public:
    struct Reply {
        int status = 200;
        QJsonObject body;
    };
    typedef std::function<Reply(const QString &, const QString &, const QUrlQuery &, const QJsonObject &)> Handler;

    explicit ExcelIpcServer(QObject *parent = nullptr);
    ~ExcelIpcServer() override;

    bool listen(const QString &session, const QString &token, QString *error = nullptr);
    quint16 port() const;
    void close();
    void drain();
    void setHandler(const Handler &handler);

signals:
    void drained();

private slots:
    void acceptConnections();
    void readClient();
    void clientDisconnected();
    void clientTimedOut();

private:
    struct Client;
    void finish(Client *client, const Reply &reply);
    void fail(Client *client, int status, const char *code);
    bool authenticate(const QByteArray &headers) const;
    bool parseRequest(Client *client, QString *method, QString *path,
                      QUrlQuery *query, QJsonObject *body, int *status,
                      const char **code) const;
    void removeClient(Client *client);

    QTcpServer *m_server;
    QString m_session;
    QByteArray m_token;
    Handler m_handler;
    QList<Client *> m_clients;
    bool m_draining = false;
};

#endif

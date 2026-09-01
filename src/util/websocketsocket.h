#ifndef WEBSOCKETSOCKET_H
#define WEBSOCKETSOCKET_H

#include "socket.h"

#include <QAbstractSocket>
#include <QtWebSockets/QWebSocketServer>

class QWebSocket;

class WebSocketServerSocket : public ServerSocket
{
    Q_OBJECT

public:
    WebSocketServerSocket();

    bool listen() override;
    void daemonize() override;
    QString listeningAddress() const override;
    quint16 listeningPort() const override;

private slots:
    void processNewConnection();
    void allowOrigin(class QWebSocketCorsAuthenticator *authenticator);

private:
    QWebSocketServer *server;
};

class WebSocketClientSocket : public ClientSocket
{
    Q_OBJECT

public:
    explicit WebSocketClientSocket(QWebSocket *socket);

    void connectToHost() override;
    void disconnectFromHost() override;
    void send(const QByteArray &message) override;
    bool isConnected() const override;
    QString peerName() const override;
    QString peerAddress() const override;

private slots:
    void getMessage(const QString &message);
    void rejectBinary(const QByteArray &message);
    void raiseError(QAbstractSocket::SocketError socket_error);

private:
    QWebSocket *const socket;

    static bool isValidProtocolFrame(const QByteArray &message);
};

#endif

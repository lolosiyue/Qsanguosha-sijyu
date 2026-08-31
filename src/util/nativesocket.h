#ifndef _NATIVESOCKET_H
#define _NATIVESOCKET_H

#include "socket.h"
#include "protocol/protocol-runtime.h"

#include <QAbstractSocket>
#include <QTcpServer>
#include <QTcpSocket>

class QUdpSocket;

class NativeServerSocket : public ServerSocket
{
    Q_OBJECT

public:
    NativeServerSocket();

    virtual bool listen();
    virtual void daemonize();
    QString listeningAddress() const override;
    quint16 listeningPort() const override;

private slots:
    void processNewConnection();
    void processNewDatagram();

private:
    QTcpServer *server;
    QUdpSocket *daemon;
};


class NativeClientSocket : public ClientSocket
{
    Q_OBJECT

public:
    NativeClientSocket();
    NativeClientSocket(QTcpSocket *socket);

    virtual void connectToHost();
    void connectToHost(const QString &host, quint16 port);
    virtual void disconnectFromHost();
    void abort();
    virtual void send(const QByteArray &message);
    virtual bool isConnected() const;
    virtual QString peerName() const;
    virtual QString peerAddress() const;
    QAbstractSocket::SocketError lastError() const;
    QString errorString() const;

private slots:
    void getMessage();
    void raiseError(QAbstractSocket::SocketError socket_error);

private:
    QTcpSocket *const socket;
    QSanProtocol::ProtocolFrameBuffer m_frameBuffer;

    void init();
};

#endif

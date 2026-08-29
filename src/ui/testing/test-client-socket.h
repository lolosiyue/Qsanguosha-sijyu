#ifndef TEST_CLIENT_SOCKET_H
#define TEST_CLIENT_SOCKET_H

#include "socket.h"

class TestClientSocket final : public ClientSocket
{
    Q_OBJECT

public:
    explicit TestClientSocket(QObject *parent = nullptr);

    void connectToHost() override;
    void send(const QByteArray &message) override;
    bool isConnected() const override;
    QString peerName() const override;
    QString peerAddress() const override;

    void injectServerPacket(const QString &packetJson);
    QList<QString> sentPackets() const;
    void clearSentPackets();

public slots:
    void disconnectFromHost() override;

signals:
    void packetSent(const QString &packetJson);

private:
    bool m_connected;
    QList<QString> m_sentPackets;
};

#endif

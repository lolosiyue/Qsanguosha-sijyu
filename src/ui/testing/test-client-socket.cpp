#include "test-client-socket.h"

TestClientSocket::TestClientSocket(QObject *parent)
    : m_connected(false)
{
    setParent(parent);
}

void TestClientSocket::connectToHost()
{
    if (m_connected)
        return;
    m_connected = true;
    emit connected();
}

void TestClientSocket::send(const QByteArray &message)
{
    const QString text = QString::fromUtf8(message);
    m_sentPackets << text;
    emit packetSent(text);
}

bool TestClientSocket::isConnected() const
{
    return m_connected;
}

QString TestClientSocket::peerName() const
{
    return QStringLiteral("local-response-ui");
}

QString TestClientSocket::peerAddress() const
{
    return QStringLiteral("local");
}

void TestClientSocket::injectServerPacket(const QString &packetJson)
{
    emit message_got(packetJson.toUtf8());
}

QList<QString> TestClientSocket::sentPackets() const
{
    return m_sentPackets;
}

void TestClientSocket::clearSentPackets()
{
    m_sentPackets.clear();
}

void TestClientSocket::disconnectFromHost()
{
    if (!m_connected)
        return;
    m_connected = false;
    emit disconnected();
}

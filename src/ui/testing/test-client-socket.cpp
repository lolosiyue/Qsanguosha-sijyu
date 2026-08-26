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

void TestClientSocket::send(const QString &message)
{
    m_sentPackets << message;
    emit packetSent(message);
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
    // The signal uses a legacy const char * contract. Retain the QByteArray as
    // a member so its backing storage remains valid for the complete delivery.
    m_injectedPacket = packetJson.toUtf8();
    emit message_got(m_injectedPacket.constData());
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
